//===- Signals.cpp - Generic AmigaOS Signals Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helpful functions for dealing with the possibility of
// AmigaOS signals occurring while your program is running.
//
//===----------------------------------------------------------------------===//
//
// This is hack job since AmigaOS Signal handling is poor with Clib2
//
//===----------------------------------------------------------------------===//

#include "AmigaOS.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/config.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/ExitCodes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>

#include <signal.h>
#include <sys/stat.h>
#include <dlfcn.h>

using namespace llvm;


static RETSIGTYPE SignalHandler(int Sig);  // defined below.
static RETSIGTYPE InfoSignalHandler(int Sig);  // defined below.

using SignalHandlerFunctionType = void (*)();
/// The function to call if ctrl-c is pressed.
static std::atomic<SignalHandlerFunctionType> InterruptFunction =
    ATOMIC_VAR_INIT(nullptr);
static std::atomic<SignalHandlerFunctionType> InfoSignalFunction =
    ATOMIC_VAR_INIT(nullptr);
/// The function to call on SIGPIPE (one-time use only).
static std::atomic<SignalHandlerFunctionType> OneShotPipeSignalFunction =
    ATOMIC_VAR_INIT(nullptr);

namespace {
/// Signal-safe removal of files.
/// Inserting and erasing from the list isn't signal-safe, but removal of files
/// themselves is signal-safe. Memory is freed when the head is freed, deletion
/// is therefore not signal-safe either.
class FileToRemoveList {
  std::atomic<char *> Filename = ATOMIC_VAR_INIT(nullptr);
  std::atomic<FileToRemoveList *> Next = ATOMIC_VAR_INIT(nullptr);

  FileToRemoveList() = default;
  // Not signal-safe.
  FileToRemoveList(const std::string &str) : Filename(strdup(str.c_str())) {}

public:
  // Not signal-safe.
  ~FileToRemoveList() {
    if (FileToRemoveList *N = Next.exchange(nullptr))
      delete N;
    if (char *F = Filename.exchange(nullptr))
      free(F);
  }

  // Not signal-safe.
  static void insert(std::atomic<FileToRemoveList *> &Head,
                     const std::string &Filename) {
    // Insert the new file at the end of the list.
    FileToRemoveList *NewHead = new FileToRemoveList(Filename);
    std::atomic<FileToRemoveList *> *InsertionPoint = &Head;
    FileToRemoveList *OldHead = nullptr;
    while (!InsertionPoint->compare_exchange_strong(OldHead, NewHead)) {
      InsertionPoint = &OldHead->Next;
      OldHead = nullptr;
    }
  }

  // Not signal-safe.
  static void erase(std::atomic<FileToRemoveList *> &Head,
                    const std::string &Filename) {
    // Use a lock to avoid concurrent erase: the comparison would access
    // free'd memory.
    static ManagedStatic<sys::SmartMutex<true>> Lock;
    sys::SmartScopedLock<true> Writer(*Lock);

    for (FileToRemoveList *Current = Head.load(); Current;
         Current = Current->Next.load()) {
      if (char *OldFilename = Current->Filename.load()) {
        if (OldFilename != Filename)
          continue;
        // Leave an empty filename.
        OldFilename = Current->Filename.exchange(nullptr);
        // The filename might have become null between the time we
        // compared it and we exchanged it.
        if (OldFilename)
          free(OldFilename);
      }
    }
  }

  // Signal-safe.
  static void removeAllFiles(std::atomic<FileToRemoveList *> &Head) {
    // If cleanup were to occur while we're removing files we'd have a bad time.
    // Make sure we're OK by preventing cleanup from doing anything while we're
    // removing files. If cleanup races with us and we win we'll have a leak,
    // but we won't crash.
    FileToRemoveList *OldHead = Head.exchange(nullptr);

    for (FileToRemoveList *currentFile = OldHead; currentFile;
         currentFile = currentFile->Next.load()) {
      // If erasing was occuring while we're trying to remove files we'd look
      // at free'd data. Take away the path and put it back when done.
      if (char *path = currentFile->Filename.exchange(nullptr)) {
        // Get the status so we can determine if it's a file or directory. If we
        // can't stat the file, ignore it.
        struct stat buf;
        if (stat(path, &buf) != 0)
          continue;

        // If this is not a regular file, ignore it. We want to prevent removal
        // of special files like /dev/null, even if the compiler is being run
        // with the super-user permissions.
        if (!S_ISREG(buf.st_mode))
          continue;

        // Otherwise, remove the file. We ignore any errors here as there is
        // nothing else we can do.
        unlink(path);

        // We're done removing the file, erasing can safely proceed.
        currentFile->Filename.exchange(path);
      }
    }

    // We're done removing files, cleanup can safely proceed.
    Head.exchange(OldHead);
  }
};
static std::atomic<FileToRemoveList *> FilesToRemove = ATOMIC_VAR_INIT(nullptr);

/// Clean up the list in a signal-friendly manner.
/// Recall that signals can fire during llvm_shutdown. If this occurs we should
/// either clean something up or nothing at all, but we shouldn't crash!
struct FilesToRemoveCleanup {
  // Not signal-safe.
  ~FilesToRemoveCleanup() {
    FileToRemoveList *Head = FilesToRemove.exchange(nullptr);
    if (Head)
      delete Head;
  }
};
} // namespace

static StringRef Argv0;

/// Signals that represent requested termination. There's no bug or failure, or
/// if there is, it's not our direct responsibility. For whatever reason, our
/// continued execution is no longer desirable.
static const int IntSigs[] = {
  SIGINT, SIGTERM
};

/// Signals that represent that we have a bug, and our prompt termination has
/// been ordered.
static const int KillSigs[] = {
  SIGILL,  SIGABRT, SIGFPE,  SIGSEGV
};

/// Signals that represent requests for status.
//static const int InfoSigs[] = {
//};

static const size_t NumSigs =
    array_lengthof(IntSigs) + array_lengthof(KillSigs);
    //array_lengthof(InfoSigs) + 1 /* SIGPIPE */;


static std::atomic<unsigned> NumRegisteredSignals = ATOMIC_VAR_INIT(0);
static struct {
  sig_t SA;
  int SigNo;
} RegisteredSignalInfo[NumSigs];


static void RegisterHandlers() { // Not signal-safe.
  // The mutex prevents other threads from registering handlers while we're
  // doing it. We also have to protect the handlers and their count because
  // a signal handler could fire while we're registeting handlers.
  static ManagedStatic<sys::SmartMutex<true>> SignalHandlerRegistrationMutex;
  sys::SmartScopedLock<true> Guard(*SignalHandlerRegistrationMutex);

  // If the handlers are already registered, we're done.
  if (NumRegisteredSignals.load() != 0)
    return;


  enum class SignalKind { IsKill, IsInfo };
  auto registerHandler = [&](int Signal, SignalKind Kind) {
    unsigned Index = NumRegisteredSignals.load();
    assert(Index < array_lengthof(RegisteredSignalInfo) &&
           "Out of space for signal handlers!");

    sig_t NewHandler;

    switch (Kind) {
    case SignalKind::IsKill:
      NewHandler = SignalHandler;      
      break;
    case SignalKind::IsInfo:
      NewHandler = InfoSignalHandler;      
      break;
    }

    RegisteredSignalInfo[Index].SigNo = Signal;
    RegisteredSignalInfo[Index].SA = signal(Signal, NewHandler);        
    ++NumRegisteredSignals;
  };

  for (auto S : IntSigs)
    registerHandler(S, SignalKind::IsKill);
  for (auto S : KillSigs)
    registerHandler(S, SignalKind::IsKill);
 // if (OneShotPipeSignalFunction)
 //   registerHandler(SIGPIPE, SignalKind::IsKill);
  //for (auto S : InfoSigs)
    //registerHandler(S, SignalKind::IsInfo);
}

void sys::unregisterHandlers() {
  // Restore all of the signal handlers to how they were before we showed up.
  for (unsigned i = 0, e = NumRegisteredSignals.load(); i != e; ++i) {
      signal(RegisteredSignalInfo[i].SigNo, RegisteredSignalInfo[i].SA);      
    --NumRegisteredSignals;
  }
}

/// Process the FilesToRemove list.
static void RemoveFilesToRemove() {
  FileToRemoveList::removeAllFiles(FilesToRemove);
}

void sys::CleanupOnSignal(uintptr_t Context) {
  int Sig = (int)Context;

  //if (llvm::is_contained(InfoSigs, Sig)) {
  //  InfoSignalHandler(Sig);
  //  return;
 // }

  RemoveFilesToRemove();

  if (llvm::is_contained(IntSigs, Sig)) // || Sig == SIGPIPE)
    return;

  llvm::sys::RunSignalHandlers();
}

// The signal handler that runs.
static RETSIGTYPE SignalHandler(int Sig) {
  // Restore the signal behavior to default, so that the program actually
  // crashes when we return and the signal reissues.  This also ensures that if
  // we crash in our signal handler that the program will terminate immediately
  // instead of recursing in the signal handler.
  sys::unregisterHandlers();

  // Unmask all potentially blocked kill signals.
  sigset_t SigMask;

#define sigfillset(what)    (*(what) = ~(0), 0)
  sigfillset(&SigMask);
  sigprocmask(SIG_UNBLOCK, &SigMask, nullptr);

  {
    RemoveFilesToRemove();
/*
    if (Sig == SIGPIPE)
      if (auto OldOneShotPipeFunction =
              OneShotPipeSignalFunction.exchange(nullptr))
        return OldOneShotPipeFunction();
*/

    bool IsIntSig = llvm::is_contained(IntSigs, Sig);
    if (IsIntSig)
      if (auto OldInterruptFunction = InterruptFunction.exchange(nullptr))
        return OldInterruptFunction();

    // if (Sig == SIGPIPE || IsIntSig) {
    if (IsIntSig) {        
      raise(Sig); // Execute the default handler.
      return;
    }
  }

  // Otherwise if it is a fault (like SEGV) run any handler.
  llvm::sys::RunSignalHandlers();

}

static RETSIGTYPE InfoSignalHandler(int Sig) {
  SaveAndRestore<int> SaveErrnoDuringASignalHandler(errno);
  if (SignalHandlerFunctionType CurrentInfoFunction = InfoSignalFunction)
    CurrentInfoFunction();
}

void llvm::sys::RunInterruptHandlers() {
  RemoveFilesToRemove();
}

void llvm::sys::SetInterruptFunction(void (*IF)()) {
  InterruptFunction.exchange(IF);
  RegisterHandlers();
}

void llvm::sys::SetInfoSignalFunction(void (*Handler)()) {
  InfoSignalFunction.exchange(Handler);
  RegisterHandlers();
}

void llvm::sys::SetOneShotPipeSignalFunction(void (*Handler)()) {
  OneShotPipeSignalFunction.exchange(Handler);
  RegisterHandlers();
}

void llvm::sys::DefaultOneShotPipeSignalHandler() {
  // Send a special return code that drivers can check for, from sysexits.h.
  exit(EX_IOERR);
}

// The public API
bool llvm::sys::RemoveFileOnSignal(StringRef Filename,
                                   std::string* ErrMsg) {
  // Ensure that cleanup will occur as soon as one file is added.
  static ManagedStatic<FilesToRemoveCleanup> FilesToRemoveCleanup;
  *FilesToRemoveCleanup;
  FileToRemoveList::insert(FilesToRemove, Filename.str());
  RegisterHandlers();
  return false;
}

// The public API
void llvm::sys::DontRemoveFileOnSignal(StringRef Filename) {
  FileToRemoveList::erase(FilesToRemove, Filename.str());
}

/// Add a function to be called when a signal is delivered to the process. The
/// handler can have a cookie passed to it to identify what instance of the
/// handler it is.
void llvm::sys::AddSignalHandler(sys::SignalHandlerCallback FnPtr,
                                 void *Cookie) { // Signal-safe.
  insertSignalHandler(FnPtr, Cookie);
  RegisterHandlers();
}

// In the case of a program crash or fault, print out a stack trace so that the
// user has an indication of why and where we died.
//
// On glibc systems we have the 'backtrace' function, which works nicely, but
// doesn't demangle symbols.

static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  return false;
}


void llvm::sys::PrintStackTrace(raw_ostream &OS, int Depth) {
    NOTIMPLEMENTED;
}

static void PrintStackTraceSignalHandler(void *) {
  sys::PrintStackTrace(llvm::errs());
}

void llvm::sys::DisableSystemDialogsOnCrash() {}

/// When an error signal (such as SIGABRT or SIGSEGV) is delivered to the
/// process, print a stack trace and then exit.
void llvm::sys::PrintStackTraceOnErrorSignal(StringRef Argv0,
                                             bool DisableCrashReporting) {
  ::Argv0 = Argv0;
  AddSignalHandler(PrintStackTraceSignalHandler, nullptr);
}
