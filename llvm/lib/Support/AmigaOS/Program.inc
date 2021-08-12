//===- llvm/Support/AmigaOS/Program.inc -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific portion of the Program class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic AmigaOS code that
//===          is guaranteed to work on *all* AmigaOS variants.
//===----------------------------------------------------------------------===//

#define DEBUG

#include "llvm/Support/Program.h"

#include "AmigaOS.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

#include <list>

#pragma GCC system_header

using namespace llvm;
using namespace sys;

struct SegmentData {
    BPTR sd_SegmentList;
    struct DosResidentSeg *sd_ResidentSegment;   

    SegmentData() :  sd_SegmentList(ZERO), sd_ResidentSegment(NULL) {}
};

struct ProcessHandle {
    struct Process *ph_Process;
    procid_t ph_Pid; 
    uint32 ph_RefCount;
    int32 ph_ReturnCode; 
    struct SegmentData ph_SegmentData;

    ProcessHandle() : ph_Process(NULL), ph_Pid(0), ph_RefCount(0), ph_ReturnCode(0), ph_SegmentData() {}
};


#define DOSRESIDENTSEMAPHORENAME "DosResident"

class ChildProcessList
{

    private:
        std::list<ProcessHandle *> Children;
        APTR Mutex;

        void Lock();
        void Unlock();
        ChildProcessList();
        bool HasChildren();
        ~ChildProcessList();


        static struct SignalSemaphore *DOSResidentSemaphore() {
            static struct SignalSemaphore *instance = IExec->FindSemaphore(DOSRESIDENTSEMAPHORENAME);
            return instance;
        }

        static inline bool ObtainDOSResidentSemaphore()
        {
            struct SignalSemaphore *sema=DOSResidentSemaphore();
            if (NULL != sema) {
                IExec->ObtainSemaphore(sema);
            }
            return (NULL != sema);
        }

        static inline void ReleaseDOSResidentSemaphore()
        {
            struct SignalSemaphore *sema=DOSResidentSemaphore();
            if (NULL != sema) {
                IExec->ReleaseSemaphore(sema);
            }
        }

    public:
        enum WaitStatus { 
            Success=0,
            TimeOut=1,
            NotStarted=2,
            InternalError=3
        };


        static ChildProcessList& getInstance()
        {
            static ChildProcessList    instance; 
            return instance;
        }

        ChildProcessList(ChildProcessList const&) = delete;
        void operator=(ChildProcessList const&)  = delete;

        static bool LoadSegmentData(std::string const &Path, SegmentData  &SegmentData);
        static void FreeSegmentData(SegmentData  &SegmentData);


        static uint32 AddRef(struct ProcessHandle *Handle);
        static uint32 Release(struct ProcessHandle *Handle);
        static ProcessHandle *CreateProcess(SegmentData  const &SegmentData, CONST_STRPTR Name, CONST_STRPTR Args, BPTR Input, int32 CloseInput, BPTR Output, int32 CloseOutput, BPTR Error, int32 CloseError, STRPTR *EnvStrs);
        static WaitStatus WaitForProcess(ProcessHandle *Handle, uint32 TimeOutSeconds, bool WaitUntilChildTerminates);     


};

// Implemenation


void ChildProcessList::Lock()
{
    FUNCENTER;
    if (Mutex) IExec->MutexObtain(Mutex);
    FUNCEXIT;
    
}
void ChildProcessList::Unlock()
{
    FUNCENTER;
    if (Mutex) IExec->MutexRelease(Mutex);
    FUNCEXIT;
}

ChildProcessList::ChildProcessList()
{
    FUNCENTER;
    Mutex = IExec->AllocSysObjectTags(ASOT_MUTEX, TAG_END);        
    FUNCEXIT;
} 

bool ChildProcessList::HasChildren()
{
    FUNCENTER;
    Lock();
    auto result = !Children.empty();
    Unlock();
    FUNCEXIT;
    return result;
}



ChildProcessList::~ChildProcessList()
{
    FUNCENTER;
    while(HasChildren())
    {
        DPRINTF("*************************Waiting**********************\n");
        DPRINTF("%Running processes %ld\n", Children.size());
        DPRINTF("*************************Waiting**********************\n");
        IDOS->Delay(10);
    }
    if (Mutex)
        IExec->FreeSysObject(ASOT_MUTEX, Mutex);
    FUNCEXIT;                
}               



bool ChildProcessList::LoadSegmentData(std::string const &Path, SegmentData  &SegmentData)
{
    FUNCENTER;
    SegmentData = ::SegmentData();
    auto path = Path.c_str();
    auto seglist = IDOS->LoadSeg(path);
    if (seglist)
    {
        DPRINTF("Loaded Segment for %s\n", path);
        SegmentData.sd_SegmentList = seglist;
    }
    else {
        if (ObtainDOSResidentSemaphore()) {
            DPRINTF("ObtainDOSResidentSemaphore\n");
            auto resident  = IDOS->FindSegment(path,NULL,FALSE);
	        if (NULL == resident) {
		        resident = IDOS->FindSegment(path,NULL,TRUE);
                if (NULL != resident)
                {
                    if(!((resident->seg_UC < 0) && ((resident->seg_UC > CMD_INTERNAL) || (resident->seg_UC <= CMD_DISABLED)))){	
                        DPRINTF("Loaded Resident for %s\n", path);		            
                        if(resident->seg_UC > 0) {
				            resident->seg_UC++;
                        }     
                        SegmentData.sd_ResidentSegment = resident;
                        SegmentData.sd_SegmentList = resident->seg_Seg;
                    }
                }
            }            
            ReleaseDOSResidentSemaphore();
        }
        else {
            DPRINTF("Failed to ObtainDOSResidentSemaphore\n");
        }
    }
    FUNCEXIT;
    return (ZERO != SegmentData.sd_SegmentList);
}

void ChildProcessList::FreeSegmentData(SegmentData  &SegmentData)
{
    FUNCENTER;
    if (NULL == SegmentData.sd_ResidentSegment)
    {
        if (ZERO != SegmentData.sd_SegmentList)
            IDOS->UnLoadSeg(SegmentData.sd_SegmentList);
    }
    else {
        if (ObtainDOSResidentSemaphore()) {
            auto resident = SegmentData.sd_ResidentSegment; 
            if(resident->seg_UC > 0) {
				resident->seg_UC--;
            }            
            ReleaseDOSResidentSemaphore();
        }
    }
    SegmentData = ::SegmentData();
    FUNCEXIT;
}



uint32 ChildProcessList::AddRef(struct ProcessHandle *Handle)
{            
    FUNCENTER;
    ChildProcessList& cpl = getInstance();
    cpl.Lock();
    if (1 == Handle->ph_RefCount)
    {   
        DPRINTF("AddRef Child PID:%ld\n", Handle->ph_Pid);        
        cpl.Children.push_back(Handle);
    }                
    Handle->ph_RefCount++;    
    DPRINTF("AddRef Child PID:%ld RefCount=%lu\n",Handle->ph_Pid,Handle->ph_RefCount);    
    cpl.Unlock();
    FUNCEXIT;
    return Handle->ph_RefCount;
}

uint32 ChildProcessList::Release(struct ProcessHandle *Handle)
{
    FUNCENTER;
    ChildProcessList& cpl = getInstance();        
    cpl.Lock();
    uint32 refCount = (--Handle->ph_RefCount);    
    DPRINTF("Releasing Child PID:%ld RefCount=%lu\n",Handle->ph_Pid,Handle->ph_RefCount);        
    if (0 == refCount)
    {
        DPRINTF("Releasing Child PID:%ld\n",Handle->ph_Pid);        
        getInstance().Children.remove(Handle);
        FreeSegmentData(Handle->ph_SegmentData);
        IExec->FreeVec(Handle);
    }
    cpl.Unlock();
    FUNCEXIT;
    return refCount;
}    

static void FinalCode(int32 return_code, int32 final_data)
{
    FUNCENTER;
    struct ProcessHandle *handle = reinterpret_cast<struct ProcessHandle *>(final_data);
    if (NULL != handle) {
        handle->ph_ReturnCode = return_code;
        ChildProcessList::Release(handle);          
    }
    FUNCEXIT;
}     

ProcessHandle *ChildProcessList::CreateProcess(SegmentData  const &SegmentData, CONST_STRPTR Name, CONST_STRPTR Args, BPTR Input, int32 CloseInput, BPTR Output, int32 CloseOutput, BPTR Error, int32 CloseError, STRPTR *EnvStrs)
{      
    FUNCENTER; 
    DPRINTF("CreateProcess %s\n",Name);
    struct ProcessHandle *handle = (struct ProcessHandle*)IExec->AllocVecTags(sizeof(ProcessHandle), AVT_Type, MEMF_SHARED,AVT_ClearWithValue,0,  TAG_DONE);
    if (NULL != handle)
    {
        handle->ph_SegmentData=SegmentData;
        DPRINTF("CreateProcess %s allocated handled\n",Name);
        IExec->Forbid();
        if(handle->ph_Process  = IDOS->CreateNewProcTags(
            NP_Seglist,					SegmentData.sd_SegmentList,            
            NP_Name,					Name,
            NP_StackSize,				2000000,
            NP_Cli,						true,
            NP_Child,					true,
            NP_Arguments,				Args,
            NP_Input,					Input,
            NP_CloseInput,				CloseInput,
            NP_Output,					Output, 
            NP_CloseOutput,				CloseOutput,
            NP_Error,					Error,
            NP_CloseError,				CloseError,
            NP_FinalCode,               FinalCode,
            NP_FinalData,               handle,
            NP_CopyVars,                TRUE, 
            NP_LocalVars,               EnvStrs,
            TAG_DONE
        ))            
        {                    
            IExec->SuspendTask((struct Task *)handle->ph_Process, 0L);
            IExec->Permit();
            DPRINTF("CreateProcess %s Process created\n",Name);
            handle->ph_Pid = IDOS->GetPID(handle->ph_Process, GPID_PROCESS);                    
            handle->ph_RefCount=1;
            AddRef(handle);
            IExec->RestartTask((struct Task *)handle->ph_Process, 0L);
            DPRINTF("CreateProcess %s  Restarted %p Pid %ld\n",Name, handle->ph_Process, handle->ph_Pid);            
        }
        else
        {            
            IExec->Permit();
            IExec->FreeVec(handle);            
            handle = NULL;
            DPRINTF("CreateProcess %s Failed!!!!\n", Name);
        }
    }
    FUNCEXIT;
    return handle;
}


static uint32 FindHandleHook(REG(a0, struct Hook *hook) UNUSED,
                    REG(a2, struct ProcessHandle *Handle),
                    REG(a1, struct Process *proc) )
{
    return (proc->pr_ProcessID == Handle->ph_Pid) ? TRUE : FALSE;
}

static uint32 FindChildHandle(struct DOSIFace *idos, struct ProcessHandle *Handle)
{
    struct Hook H;
    H.h_Entry = reinterpret_cast<uint32 (*)()>(FindHandleHook);
    return (uint32)idos->ProcessScan(&H, Handle, 0);
}       

ChildProcessList::WaitStatus ChildProcessList::WaitForProcess(ProcessHandle *Handle, uint32 TimeOutSeconds, bool WaitUntilChildTerminates)        
{         
    FUNCENTER;                    
               
    uint32 error = 0;
    struct MsgPort *TimerMP = NULL;
    struct TimeRequest *TimerIO = NULL;
    struct Message *TimerMSG = NULL;
    uint32 timerSignal   = 0;
    uint32 scanSignal    = SIGBREAKF_CTRL_E; 
    bool timeout = false;  


    WaitStatus result= WaitStatus::NotStarted;
    if (NULL!=Handle) {
        DPRINTF("Waiting for for Pid:%ld\n", Handle->ph_Pid);
        if (!WaitUntilChildTerminates) {
            DPRINTF("Setting TimeOut for for Pid:%ld\n", Handle->ph_Pid);
            TimerMP = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);    
            if (TimerMP != NULL) {                    
                TimerIO = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,
                    ASOIOR_Size, sizeof(struct TimeRequest),
                    ASOIOR_ReplyPort, TimerMP,
                    TAG_END);      
                if (TimerIO != NULL) {	
                    if (!(error = IExec->OpenDevice( TIMERNAME, UNIT_VBLANK,(struct IORequest *) TimerIO, 0L))) {
                        timerSignal   = 1L << TimerMP->mp_SigBit;
                        TimerIO->Request.io_Command = TR_ADDREQUEST;
                        TimerIO->Time.Seconds       = TimeOutSeconds;
                        TimerIO->Time.Microseconds  = 0;
                        IExec->SendIO((struct IORequest *)TimerIO);
                        DPRINTF("Waiting %ld seconds\n", TimeOutSeconds);
                    }
                    else {
                        DPRINTF("Failed to Open Timer Device\n");
                    }
                }
                else {
                    DPRINTF("Failed to allocate Timer IORequest\n");    
                }
            } 
            else {
                DPRINTF("Failed to allocate Timer MessagePort\n");
            }           
        }   
        DPRINTF("Install watcher for for Pid:%ld\n", Handle->ph_Pid);                     
        if(IDOS->NotifyProcListChange((struct Process *)IExec->FindTask(NULL),SIGBREAKB_CTRL_E,0)) {
            result= WaitStatus::Success;
            while((!timeout) && FindChildHandle(IDOS, Handle)) {
                uint32 signals = IExec->Wait(timerSignal | scanSignal);
                timeout = (signals & timerSignal) != 0;                    
                if((!timeout) && (timerSignal!=0)) {
                    // not a timeout so cleanup timer
                    IExec->AbortIO((struct IORequest *)TimerIO);
                    IExec->WaitIO((struct IORequest *)TimerIO);
                }                                        
            }
            IDOS->NotifyProcListChange(NULL, NPLC_END, 0);
            if (timeout) {
                // pull message
                IExec->GetMsg(TimerMP);
                DPRINTF("ALERT %ld has timedout!!\n", Handle->ph_Pid); 
                // attempt signal to shutdown
                IExec->Signal((struct Task *)Handle->ph_Process, SIGBREAKF_CTRL_C);
                // Wait a second to see if it shuts down
                IDOS->Delay(TICKS_PER_SECOND * 1);
                result = WaitStatus::TimeOut;            
            }              
            else {
                DPRINTF("No longer running Pid:%ld\n", Handle->ph_Pid);                     
            }
        }
        else {   
            DPRINTF("Failed to hook NotifyProcListChange\n");
            result = WaitStatus::InternalError;
        }         
        // Cleanup Timer if allocated
        if (NULL != TimerMP) {
            DPRINTF("Cleaning up Timer for Pid:%ld\n", Handle->ph_Pid);                     
            if (NULL != TimerIO) {
                if (!error)   
                    IExec->CloseDevice((struct IORequest *) TimerIO);
                IExec->FreeSysObject(ASOT_IOREQUEST, TimerIO);                    
            }
            IExec->FreeSysObject(ASOT_PORT, TimerMP);    
        }
    }
    FUNCEXIT;
    return result;

}


ProcessInfo::ProcessInfo() : Pid(0), Process(NULL), ReturnCode(0) {}



ProcessInfo llvm::sys::Wait(const ProcessInfo &PI, unsigned SecondsToWait,
                            bool WaitUntilTerminates, std::string *ErrMsg,
                            Optional<ProcessStatistics> *ProcStat) {

    assert(PI.Pid && "invalid pid to wait on, process not started?");
    assert((PI.Process && PI.Process != NULL) &&
          "invalid process handle to wait on, process not started?");
    FUNCENTER;
        
    DPRINTF("waiting for Pid: %ld\n", PI.Pid);
    ProcessInfo WaitResult = PI;
    struct ProcessHandle *handle = (struct ProcessHandle *)WaitResult.Process;
    switch (ChildProcessList::WaitForProcess(handle, SecondsToWait, WaitUntilTerminates)) {
        case ChildProcessList::WaitStatus::Success:
            WaitResult.ReturnCode =  handle->ph_ReturnCode;
            DPRINTF("Pid: %ld has terminated!\n", PI.Pid);            
            break;            
        case ChildProcessList::WaitStatus::TimeOut:
            WaitResult.ReturnCode = -2;
            DPRINTF("Pid: %ld has timedout!\n", PI.Pid);                   
            break;
        case ChildProcessList::WaitStatus::NotStarted:
            WaitResult.ReturnCode = -1;
            DPRINTF("Pid: %ld was not started!\n", PI.Pid);                 
            break;
        case ChildProcessList::WaitStatus::InternalError:
            WaitResult.ReturnCode = -2;
            DPRINTF("Pid: %ld had Internal OS Error!\n", PI.Pid);            
            break;
    }
    if (handle) {        
        WaitResult.Process = NULL;        
        ChildProcessList::Release(handle);
    }    
    FUNCEXIT;
    return WaitResult;
}

std::string static flattenArguments(ArrayRef<StringRef> Args) {
   std::string Command;
   for (StringRef Arg : Args) {     
       if (Command.length())
            Command.push_back(' ');
       Command += Arg;      
   }  
   Command.push_back('\n');
   return Command;
}



bool static SplitEnv(std::string const var, std::string &name,  std::string &value) {
    FUNCENTER;
    static std::string delimiter = "=";
    std::string s=var;
    std::string token;
    size_t pos = 0;
    int count = 0;               
    while ((pos = s.find(delimiter)) != std::string::npos) {         
        name = s.substr(0, pos);
        if (name.length()) count++;
        s.erase(0, pos + delimiter.length());    
	    value = s;
	    if (value.length()) count++;
        DPRINTF("Parsed Env Variable Name:%s Value:%s\n", name.c_str(), value.c_str());
    }   
    FUNCEXIT;
    return count==2;
}

static void FreeEnv(std::vector<STRPTR> &envstr) {
    FUNCENTER;
    for(auto str : envstr)
    {
        if (NULL != str) free(str);
    }
    envstr.clear();
    FUNCEXIT;
}

static bool MakeEnv(ArrayRef<StringRef> Env, std::vector<STRPTR> &Envstr)
{
    FUNCENTER;

    for (StringRef var : Env)
    {
       std::string name, value;
       if (SplitEnv(var.str(), name, value))
       {
           auto pname = strdup(name.c_str());
           auto pvalue =strdup(value.c_str());
           if ((NULL == pname) || (NULL == pvalue))
           {
               if (pname) free(pname);
               if (pvalue) free(pvalue);
               FreeEnv(Envstr);
               FUNCEXIT;
               return false;
           }
           Envstr.push_back(pname);
           Envstr.push_back(pvalue);
       } 
    }
    if (!Envstr.empty()) Envstr.push_back(NULL);
    FUNCEXIT;
    return true;
}

static bool RedirectIO(Optional<StringRef> Path, BPTR &Handle, int FileNo, std::string* ErrMsg) {
    FUNCENTER;
    std::string File;
    if (!Path) // Noop
    {
        FUNCEXIT;
        return false;
    }
    // Redirect empty paths to "NIL:"
    if (Path->empty())        
        File = "NIL:";
    else
        File = std::string(*Path);
    Handle= IDOS->Open(File.c_str(), FileNo == STDIN_FILENO ? MODE_OLDFILE : MODE_READWRITE);
    if (Handle == ZERO) {
        MakeErrMsg(ErrMsg, "Cannot open file '" + File + "' for " + (FileNo == 0 ? "input" : "output"), MapAmigaIOError());        
    }
    FUNCEXIT;
    return (Handle != ZERO);    
}

static void CleanupHandles(BPTR Input, BPTR Output, BPTR Error)
{
    FUNCENTER;
    if (ZERO != Input)
        IDOS->Close(Input);
    if (ZERO != Output)
        IDOS->Close(Output);        
    if (ZERO != Error)
        IDOS->Close(Error);        
    FUNCEXIT;        
}


static bool Execute(ProcessInfo &PI, StringRef Program,
                    ArrayRef<StringRef> Args, Optional<ArrayRef<StringRef>> Env,
                    ArrayRef<Optional<StringRef>> Redirects,
                    unsigned MemoryLimit, std::string *ErrMsg,
                    BitVector *AffinityMask)
{
    FUNCENTER;

    PI = ProcessInfo();
    BPTR stdInput = ZERO;
    BPTR stdOutput= ZERO;
    BPTR stdError = ZERO;
    SegmentData segmentData;
    std::string program = Program.str();
    CONST_STRPTR programPtr = program.c_str();


    bool result = true;
    if (!Redirects.empty()) {
        DPRINTF("Configuring Redirects for %s\n", programPtr);  
        if (result = RedirectIO(Redirects[STDIN_FILENO], stdInput, STDIN_FILENO, ErrMsg)) {
            if (result = RedirectIO(Redirects[STDOUT_FILENO], stdOutput, STDOUT_FILENO, ErrMsg)) {
                if (Redirects[STDOUT_FILENO] != Redirects[STDERR_FILENO]) {
                    result = RedirectIO(Redirects[STDERR_FILENO], stdOutput, STDERR_FILENO, ErrMsg);
                }        
            }    
        }                
    }
    else {        
        DPRINTF("Standard File Handles for %s\n", programPtr);  
        stdInput = IDOS->DupFileHandle(IDOS->Input());
        stdOutput= IDOS->DupFileHandle(IDOS->Output());
        stdError = IDOS->DupFileHandle(IDOS->ErrorOutput());
    }
    

    if (result)
    {
        result =false;
        

        DPRINTF("LoadSeg for %s\n", programPtr);          
        if(ChildProcessList::LoadSegmentData(program, segmentData)) { 
            DPRINTF("Setup enviroment for %s\n", programPtr);  
            std::vector<STRPTR> envstr;
            if (MakeEnv(*Env, envstr)) {            
                DPRINTF("Creating Process for %s\n", programPtr);  
                ProcessHandle *handle  = ChildProcessList::CreateProcess(
                    segmentData, programPtr, flattenArguments(Args).c_str(), 
                    stdInput, TRUE, 
                    stdOutput, TRUE, 
                    stdError, TRUE, 
                    envstr.empty() ? NULL : envstr.data());                
                if (NULL != handle) {                    
                    PI.Process = handle;
                    PI.Pid= handle->ph_Pid;
                    DPRINTF("Launched=%s\n", programPtr);  
                    DPRINTF("PI.Process=%p\n", PI.Process);  
                    DPRINTF("PI.PID=%ld\n", PI.Pid);          
                    result =true; 
                }
                else {        
                    *ErrMsg = "Failed to start process";                             
                } 
                FreeEnv(envstr);
            }
            else {
                *ErrMsg = "Failed to allocate Environment variables";                                        
            }
        }
        else {    
            *ErrMsg = "program not executable";            
        }  
    }
    if (!result)
    {
        DPRINTF("%s Failed for:%s\n", programPtr, ErrMsg->c_str());
        ChildProcessList::FreeSegmentData(segmentData);        
        CleanupHandles(stdInput, stdOutput, stdError);
    }
    FUNCEXIT;
    return result;                    
}

int sys::CloseProcess(ProcessInfo &PI)
{
    FUNCENTER;
    int result = -1;
    if (PI.Process)
    {
       ChildProcessList::Release((struct ProcessHandle *)PI.Process);
       PI.Process = NULL;
       result = 0;
    }
    FUNCEXIT;
    return result;
}

bool sys::commandLineFitsWithinSystemLimits(StringRef Program,
                                                  ArrayRef<StringRef> Args) {

 // The documentation on AmigaDOS states thatcommand line cannot exceed 512
  // of getting invalid command line due to unaccounted factors.
  static const size_t MaxCommandStringLength = 512;
  SmallVector<StringRef, 8> FullArgs;
  FullArgs.push_back(Program);
  FullArgs.append(Args.begin(), Args.end());
  auto Result = flattenArguments(FullArgs);  
  return (Result.length() + 1) <= MaxCommandStringLength;  
}



