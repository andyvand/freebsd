//===-- SBCommandInterpreter.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-python.h"

#include "lldb/lldb-types.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Core/Listener.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/Target.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"

using namespace lldb;
using namespace lldb_private;

class CommandPluginInterfaceImplementation : public CommandObjectParsed
{
public:
    CommandPluginInterfaceImplementation (CommandInterpreter &interpreter,
                                          const char *name,
                                          lldb::SBCommandPluginInterface* backend,
                                          const char *help = NULL,
                                          const char *syntax = NULL,
                                          uint32_t flags = 0) :
    CommandObjectParsed (interpreter, name, help, syntax, flags),
    m_backend(backend) {}
    
    virtual bool
    IsRemovable() const { return true; }
    
protected:
    virtual bool
    DoExecute (Args& command, CommandReturnObject &result)
    {
        SBCommandReturnObject sb_return(&result);
        SBCommandInterpreter sb_interpreter(&m_interpreter);
        SBDebugger debugger_sb(m_interpreter.GetDebugger().shared_from_this());
        bool ret = m_backend->DoExecute (debugger_sb,(char**)command.GetArgumentVector(), sb_return);
        sb_return.Release();
        return ret;
    }
    lldb::SBCommandPluginInterface* m_backend;
};

SBCommandInterpreter::SBCommandInterpreter (CommandInterpreter *interpreter) :
    m_opaque_ptr (interpreter)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter::SBCommandInterpreter (interpreter=%p)"
                     " => SBCommandInterpreter(%p)",
                     static_cast<void*>(interpreter),
                     static_cast<void*>(m_opaque_ptr));
}

SBCommandInterpreter::SBCommandInterpreter(const SBCommandInterpreter &rhs) :
    m_opaque_ptr (rhs.m_opaque_ptr)
{
}

const SBCommandInterpreter &
SBCommandInterpreter::operator = (const SBCommandInterpreter &rhs)
{
    m_opaque_ptr = rhs.m_opaque_ptr;
    return *this;
}

SBCommandInterpreter::~SBCommandInterpreter ()
{
}

bool
SBCommandInterpreter::IsValid() const
{
    return m_opaque_ptr != NULL;
}


bool
SBCommandInterpreter::CommandExists (const char *cmd)
{
    if (cmd && m_opaque_ptr)
        return m_opaque_ptr->CommandExists (cmd);
    return false;
}

bool
SBCommandInterpreter::AliasExists (const char *cmd)
{
    if (cmd && m_opaque_ptr)
        return m_opaque_ptr->AliasExists (cmd);
    return false;
}

bool
SBCommandInterpreter::IsActive ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->IsActive ();
    return false;
}

const char *
SBCommandInterpreter::GetIOHandlerControlSequence(char ch)
{
    if (m_opaque_ptr)
        return m_opaque_ptr->GetDebugger().GetTopIOHandlerControlSequence (ch).GetCString();
    return NULL;
}

lldb::ReturnStatus
SBCommandInterpreter::HandleCommand (const char *command_line, SBCommandReturnObject &result, bool add_to_history)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::HandleCommand (command=\"%s\", SBCommandReturnObject(%p), add_to_history=%i)",
                     static_cast<void*>(m_opaque_ptr), command_line,
                     static_cast<void*>(result.get()), add_to_history);

    result.Clear();
    if (command_line && m_opaque_ptr)
    {
        result.ref().SetInteractive(false);
        m_opaque_ptr->HandleCommand (command_line, add_to_history ? eLazyBoolYes : eLazyBoolNo, result.ref());
    }
    else
    {
        result->AppendError ("SBCommandInterpreter or the command line is not valid");
        result->SetStatus (eReturnStatusFailed);
    }

    // We need to get the value again, in case the command disabled the log!
    log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API);
    if (log)
    {
        SBStream sstr;
        result.GetDescription (sstr);
        log->Printf ("SBCommandInterpreter(%p)::HandleCommand (command=\"%s\", SBCommandReturnObject(%p): %s, add_to_history=%i) => %i", 
                     static_cast<void*>(m_opaque_ptr), command_line,
                     static_cast<void*>(result.get()), sstr.GetData(),
                     add_to_history, result.GetStatus());
    }

    return result.GetStatus();
}

int
SBCommandInterpreter::HandleCompletion (const char *current_line,
                                        const char *cursor,
                                        const char *last_char,
                                        int match_start_point,
                                        int max_return_elements,
                                        SBStringList &matches)
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    int num_completions = 0;

    // Sanity check the arguments that are passed in:
    // cursor & last_char have to be within the current_line.
    if (current_line == NULL || cursor == NULL || last_char == NULL)
        return 0;

    if (cursor < current_line || last_char < current_line)
        return 0;

    size_t current_line_size = strlen (current_line);
    if (cursor - current_line > static_cast<ptrdiff_t>(current_line_size) ||
        last_char - current_line > static_cast<ptrdiff_t>(current_line_size))
        return 0;

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::HandleCompletion (current_line=\"%s\", cursor at: %" PRId64 ", last char at: %" PRId64 ", match_start_point: %d, max_return_elements: %d)",
                     static_cast<void*>(m_opaque_ptr), current_line,
                     static_cast<uint64_t>(cursor - current_line),
                     static_cast<uint64_t>(last_char - current_line),
                     match_start_point, max_return_elements);

    if (m_opaque_ptr)
    {
        lldb_private::StringList lldb_matches;
        num_completions =  m_opaque_ptr->HandleCompletion (current_line, cursor, last_char, match_start_point,
                                                           max_return_elements, lldb_matches);

        SBStringList temp_list (&lldb_matches);
        matches.AppendList (temp_list);
    }
    if (log)
        log->Printf ("SBCommandInterpreter(%p)::HandleCompletion - Found %d completions.",
                     static_cast<void*>(m_opaque_ptr), num_completions);

    return num_completions;
}

int
SBCommandInterpreter::HandleCompletion (const char *current_line,
                  uint32_t cursor_pos,
                  int match_start_point,
                  int max_return_elements,
                  lldb::SBStringList &matches)
{
    const char *cursor = current_line + cursor_pos;
    const char *last_char = current_line + strlen (current_line);
    return HandleCompletion (current_line, cursor, last_char, match_start_point, max_return_elements, matches);
}

bool
SBCommandInterpreter::HasCommands ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->HasCommands();
    return false;
}

bool
SBCommandInterpreter::HasAliases ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->HasAliases();
    return false;
}

bool
SBCommandInterpreter::HasAliasOptions ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->HasAliasOptions ();
    return false;
}

SBProcess
SBCommandInterpreter::GetProcess ()
{
    SBProcess sb_process;
    ProcessSP process_sp;
    if (m_opaque_ptr)
    {
        TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
        if (target_sp)
        {
            Mutex::Locker api_locker(target_sp->GetAPIMutex());
            process_sp = target_sp->GetProcessSP();
            sb_process.SetSP(process_sp);
        }
    }
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::GetProcess () => SBProcess(%p)", 
                     static_cast<void*>(m_opaque_ptr),
                     static_cast<void*>(process_sp.get()));

    return sb_process;
}

SBDebugger
SBCommandInterpreter::GetDebugger ()
{
    SBDebugger sb_debugger;
    if (m_opaque_ptr)
        sb_debugger.reset(m_opaque_ptr->GetDebugger().shared_from_this());
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::GetDebugger () => SBDebugger(%p)",
                     static_cast<void*>(m_opaque_ptr),
                     static_cast<void*>(sb_debugger.get()));

    return sb_debugger;
}

CommandInterpreter *
SBCommandInterpreter::get ()
{
    return m_opaque_ptr;
}

CommandInterpreter &
SBCommandInterpreter::ref ()
{
    assert (m_opaque_ptr);
    return *m_opaque_ptr;
}

void
SBCommandInterpreter::reset (lldb_private::CommandInterpreter *interpreter)
{
    m_opaque_ptr = interpreter;
}

void
SBCommandInterpreter::SourceInitFileInHomeDirectory (SBCommandReturnObject &result)
{
    result.Clear();
    if (m_opaque_ptr)
    {
        TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
        Mutex::Locker api_locker;
        if (target_sp)
            api_locker.Lock(target_sp->GetAPIMutex());
        m_opaque_ptr->SourceInitFile (false, result.ref());
    }
    else
    {
        result->AppendError ("SBCommandInterpreter is not valid");
        result->SetStatus (eReturnStatusFailed);
    }
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::SourceInitFileInHomeDirectory (&SBCommandReturnObject(%p))", 
                     static_cast<void*>(m_opaque_ptr),
                     static_cast<void*>(result.get()));
}

void
SBCommandInterpreter::SourceInitFileInCurrentWorkingDirectory (SBCommandReturnObject &result)
{
    result.Clear();
    if (m_opaque_ptr)
    {
        TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
        Mutex::Locker api_locker;
        if (target_sp)
            api_locker.Lock(target_sp->GetAPIMutex());
        m_opaque_ptr->SourceInitFile (true, result.ref());
    }
    else
    {
        result->AppendError ("SBCommandInterpreter is not valid");
        result->SetStatus (eReturnStatusFailed);
    }
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::SourceInitFileInCurrentWorkingDirectory (&SBCommandReturnObject(%p))", 
                     static_cast<void*>(m_opaque_ptr),
                     static_cast<void*>(result.get()));
}

SBBroadcaster
SBCommandInterpreter::GetBroadcaster ()
{
    Log *log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBroadcaster broadcaster (m_opaque_ptr, false);

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::GetBroadcaster() => SBBroadcaster(%p)", 
                     static_cast<void*>(m_opaque_ptr), static_cast<void*>(broadcaster.get()));

    return broadcaster;
}

const char *
SBCommandInterpreter::GetBroadcasterClass ()
{
    return Communication::GetStaticBroadcasterClass().AsCString();
}

const char * 
SBCommandInterpreter::GetArgumentTypeAsCString (const lldb::CommandArgumentType arg_type)
{
    return CommandObject::GetArgumentTypeAsCString (arg_type);
}

const char * 
SBCommandInterpreter::GetArgumentDescriptionAsCString (const lldb::CommandArgumentType arg_type)
{
    return CommandObject::GetArgumentDescriptionAsCString (arg_type);
}

bool
SBCommandInterpreter::SetCommandOverrideCallback (const char *command_name,
                                                  lldb::CommandOverrideCallback callback,
                                                  void *baton)
{
    if (command_name && command_name[0] && m_opaque_ptr)
    {
        std::string command_name_str (command_name);
        CommandObject *cmd_obj = m_opaque_ptr->GetCommandObjectForCommand(command_name_str);
        if (cmd_obj)
        {
            assert(command_name_str.empty());
            cmd_obj->SetOverrideCallback (callback, baton);
            return true;
        }
    }
    return false;
}

#ifndef LLDB_DISABLE_PYTHON

// Defined in the SWIG source file
extern "C" void 
init_lldb(void);

// these are the Pythonic implementations of the required callbacks
// these are scripting-language specific, which is why they belong here
// we still need to use function pointers to them instead of relying
// on linkage-time resolution because the SWIG stuff and this file
// get built at different times
extern "C" bool
LLDBSwigPythonBreakpointCallbackFunction (const char *python_function_name,
                                          const char *session_dictionary_name,
                                          const lldb::StackFrameSP& sb_frame,
                                          const lldb::BreakpointLocationSP& sb_bp_loc);

extern "C" bool
LLDBSwigPythonWatchpointCallbackFunction (const char *python_function_name,
                                          const char *session_dictionary_name,
                                          const lldb::StackFrameSP& sb_frame,
                                          const lldb::WatchpointSP& sb_wp);

extern "C" bool
LLDBSwigPythonCallTypeScript (const char *python_function_name,
                              void *session_dictionary,
                              const lldb::ValueObjectSP& valobj_sp,
                              void** pyfunct_wrapper,
                              std::string& retval);

extern "C" void*
LLDBSwigPythonCreateSyntheticProvider (const char *python_class_name,
                                       const char *session_dictionary_name,
                                       const lldb::ValueObjectSP& valobj_sp);


extern "C" uint32_t
LLDBSwigPython_CalculateNumChildren (void *implementor);

extern "C" void *
LLDBSwigPython_GetChildAtIndex (void *implementor, uint32_t idx);

extern "C" int
LLDBSwigPython_GetIndexOfChildWithName (void *implementor, const char* child_name);

extern "C" void *
LLDBSWIGPython_CastPyObjectToSBValue (void* data);

extern lldb::ValueObjectSP
LLDBSWIGPython_GetValueObjectSPFromSBValue (void* data);

extern "C" bool
LLDBSwigPython_UpdateSynthProviderInstance (void* implementor);

extern "C" bool
LLDBSwigPython_MightHaveChildrenSynthProviderInstance (void* implementor);

extern "C" bool
LLDBSwigPythonCallCommand (const char *python_function_name,
                           const char *session_dictionary_name,
                           lldb::DebuggerSP& debugger,
                           const char* args,
                           lldb_private::CommandReturnObject &cmd_retobj);

extern "C" bool
LLDBSwigPythonCallModuleInit (const char *python_module_name,
                              const char *session_dictionary_name,
                              lldb::DebuggerSP& debugger);

extern "C" void*
LLDBSWIGPythonCreateOSPlugin (const char *python_class_name,
                              const char *session_dictionary_name,
                              const lldb::ProcessSP& process_sp);

extern "C" bool
LLDBSWIGPythonRunScriptKeywordProcess (const char* python_function_name,
                                       const char* session_dictionary_name,
                                       lldb::ProcessSP& process,
                                       std::string& output);

extern "C" bool
LLDBSWIGPythonRunScriptKeywordThread (const char* python_function_name,
                                      const char* session_dictionary_name,
                                      lldb::ThreadSP& thread,
                                      std::string& output);

extern "C" bool
LLDBSWIGPythonRunScriptKeywordTarget (const char* python_function_name,
                                      const char* session_dictionary_name,
                                      lldb::TargetSP& target,
                                      std::string& output);

extern "C" bool
LLDBSWIGPythonRunScriptKeywordFrame (const char* python_function_name,
                                     const char* session_dictionary_name,
                                     lldb::StackFrameSP& frame,
                                     std::string& output);

extern "C" void*
LLDBSWIGPython_GetDynamicSetting (void* module,
                                  const char* setting,
                                  const lldb::TargetSP& target_sp);


#endif

void
SBCommandInterpreter::InitializeSWIG ()
{
    static bool g_initialized = false;
    if (!g_initialized)
    {
        g_initialized = true;
#ifndef LLDB_DISABLE_PYTHON
        ScriptInterpreter::InitializeInterpreter (init_lldb,
                                                  LLDBSwigPythonBreakpointCallbackFunction,
                                                  LLDBSwigPythonWatchpointCallbackFunction,
                                                  LLDBSwigPythonCallTypeScript,
                                                  LLDBSwigPythonCreateSyntheticProvider,
                                                  LLDBSwigPython_CalculateNumChildren,
                                                  LLDBSwigPython_GetChildAtIndex,
                                                  LLDBSwigPython_GetIndexOfChildWithName,
                                                  LLDBSWIGPython_CastPyObjectToSBValue,
                                                  LLDBSWIGPython_GetValueObjectSPFromSBValue,
                                                  LLDBSwigPython_UpdateSynthProviderInstance,
                                                  LLDBSwigPython_MightHaveChildrenSynthProviderInstance,
                                                  LLDBSwigPythonCallCommand,
                                                  LLDBSwigPythonCallModuleInit,
                                                  LLDBSWIGPythonCreateOSPlugin,
                                                  LLDBSWIGPythonRunScriptKeywordProcess,
                                                  LLDBSWIGPythonRunScriptKeywordThread,
                                                  LLDBSWIGPythonRunScriptKeywordTarget,
                                                  LLDBSWIGPythonRunScriptKeywordFrame,
                                                  LLDBSWIGPython_GetDynamicSetting);
#endif
    }
}

lldb::SBCommand
SBCommandInterpreter::AddMultiwordCommand (const char* name, const char* help)
{
    CommandObjectMultiword *new_command = new CommandObjectMultiword(*m_opaque_ptr,name,help);
    new_command->SetRemovable (true);
    lldb::CommandObjectSP new_command_sp(new_command);
    if (new_command_sp && m_opaque_ptr->AddUserCommand(name, new_command_sp, true))
        return lldb::SBCommand(new_command_sp);
    return lldb::SBCommand();
}

lldb::SBCommand
SBCommandInterpreter::AddCommand (const char* name, lldb::SBCommandPluginInterface* impl, const char* help)
{
    lldb::CommandObjectSP new_command_sp;
    new_command_sp.reset(new CommandPluginInterfaceImplementation(*m_opaque_ptr,name,impl,help));

    if (new_command_sp && m_opaque_ptr->AddUserCommand(name, new_command_sp, true))
        return lldb::SBCommand(new_command_sp);
    return lldb::SBCommand();
}

SBCommand::SBCommand ()
{}

SBCommand::SBCommand (lldb::CommandObjectSP cmd_sp) : m_opaque_sp (cmd_sp)
{}

bool
SBCommand::IsValid ()
{
    return (bool)m_opaque_sp;
}

const char*
SBCommand::GetName ()
{
    if (IsValid ())
        return m_opaque_sp->GetCommandName ();
    return NULL;
}

const char*
SBCommand::GetHelp ()
{
    if (IsValid ())
        return m_opaque_sp->GetHelp ();
    return NULL;
}

lldb::SBCommand
SBCommand::AddMultiwordCommand (const char* name, const char* help)
{
    if (!IsValid ())
        return lldb::SBCommand();
    if (m_opaque_sp->IsMultiwordObject() == false)
        return lldb::SBCommand();
    CommandObjectMultiword *new_command = new CommandObjectMultiword(m_opaque_sp->GetCommandInterpreter(),name,help);
    new_command->SetRemovable (true);
    lldb::CommandObjectSP new_command_sp(new_command);
    if (new_command_sp && m_opaque_sp->LoadSubCommand(name,new_command_sp))
        return lldb::SBCommand(new_command_sp);
    return lldb::SBCommand();
}

lldb::SBCommand
SBCommand::AddCommand (const char* name, lldb::SBCommandPluginInterface *impl, const char* help)
{
    if (!IsValid ())
        return lldb::SBCommand();
    if (m_opaque_sp->IsMultiwordObject() == false)
        return lldb::SBCommand();
    lldb::CommandObjectSP new_command_sp;
    new_command_sp.reset(new CommandPluginInterfaceImplementation(m_opaque_sp->GetCommandInterpreter(),name,impl,help));
    if (new_command_sp && m_opaque_sp->LoadSubCommand(name,new_command_sp))
        return lldb::SBCommand(new_command_sp);
    return lldb::SBCommand();
}

