// Copyright (c) 2018 Samsung Electronics Co., LTD
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>

#include <exception>

#include <winerror.h>

#include "vscodeprotocol.h"

#include "torelease.h"
#include "cputil.h"
#include "logger.h"

using std::string;
using std::vector;
using std::map;
using std::min;

// for convenience
using json = nlohmann::json;


void to_json(json &j, const Source &s) {
    j = json{{"name", s.name},
             {"path", s.path}};
}

void to_json(json &j, const Breakpoint &b) {
    j = json{
        {"id",       b.id},
        {"line",     b.line},
        {"verified", b.verified},
        {"message",  b.message}};
}

void to_json(json &j, const StackFrame &f) {
    j = json{
        {"id",        f.id},
        {"name",      f.name},
        {"line",      f.line},
        {"column",    f.column},
        {"endLine",   f.endLine},
        {"endColumn", f.endColumn},
        {"moduleId",  f.moduleId}};
    if (!f.source.IsNull())
        j["source"] = f.source;
}

void to_json(json &j, const Thread &t) {
    j = json{{"id",   t.id},
             {"name", t.name}};
          // {"running", t.running}
}

void to_json(json &j, const Scope &s) {
    j = json{
        {"name",               s.name},
        {"variablesReference", s.variablesReference}};

    if (s.variablesReference > 0)
    {
        j["namedVariables"] = s.namedVariables;
        // j["indexedVariables"] = s.indexedVariables;
    }
}

void to_json(json &j, const Variable &v) {
    j = json{
        {"name",               v.name},
        {"value",              v.value},
        {"type",               v.type},
        {"evaluateName",       v.evaluateName},
        {"variablesReference", v.variablesReference}};

    if (v.variablesReference > 0)
    {
        j["namedVariables"] = v.namedVariables;
        // j["indexedVariables"] = v.indexedVariables;
    }
}

static json getVSCode(const ExceptionDetails &self) {
    json details = json({});

    details["message"] = self.message;
    details["typeName"] = self.typeName;
    details["fullTypeName"] = self.fullTypeName;
    details["evaluateName"] = self.evaluateName;
    details["stackTrace"] = self.stackTrace;
    // vsdbg extention: "formattedDescription", "hresult", "source"
    // Example:
    // "formattedDescription":"**System.DivideByZeroException:** '00000:3'",
    // "hresult":-2147352558,
    // "source" : "ClassLibrary1"

    json arr = json::array();
    if (!self.innerException.empty()) {
        // INFO: Visual Studio Code does not display inner exception,
        // but vsdbg fill all nested InnerExceptions in Response.
        const auto it = self.innerException.begin();
        json inner = getVSCode(*it);
        arr.push_back(inner);
    }
    details["innerException"] = arr;

    return details;
}

void VSCodeProtocol::EmitContinuedEvent(int threadId)
{
    LogFuncEntry();

    json body;

    if (threadId != -1)
        body["threadId"] = threadId;

    body["allThreadsContinued"] = true;
    EmitEvent("continued", body);
}

void VSCodeProtocol::EmitStoppedEvent(StoppedEvent event)
{
    LogFuncEntry();

    json body;

    switch(event.reason)
    {
        case StopStep:
            body["reason"] = "step";
            break;
        case StopBreakpoint:
            body["reason"] = "breakpoint";
            break;
        case StopException:
            body["reason"] = "exception";
            break;
        case StopPause:
            body["reason"] = "pause";
            break;
        case StopEntry:
            body["reason"] = "entry";
            break;
    }

    body["description"] = event.description;
    body["text"] = event.text;
    body["threadId"] = event.threadId;
    body["allThreadsStopped"] = event.allThreadsStopped;

    // vsdbg shows additional info, but it is not a part of the protocol
    // body["line"] = event.frame.line;
    // body["column"] = event.frame.column;

    // body["source"] = event.frame.source;

    EmitEvent("stopped", body);
}

void VSCodeProtocol::EmitExitedEvent(ExitedEvent event)
{
    LogFuncEntry();
    json body;
    body["exitCode"] = event.exitCode;
    EmitEvent("exited", body);
}

void VSCodeProtocol::EmitTerminatedEvent()
{
    LogFuncEntry();
    EmitEvent("terminated", json::object());
}

void VSCodeProtocol::EmitThreadEvent(ThreadEvent event)
{
    LogFuncEntry();
    json body;

    switch(event.reason)
    {
        case ThreadStarted:
            body["reason"] = "started";
            break;
        case ThreadExited:
            body["reason"] = "exited";
            break;
    }

    body["threadId"] = event.threadId;

    EmitEvent("thread", body);
}

void VSCodeProtocol::EmitModuleEvent(ModuleEvent event)
{
    LogFuncEntry();
    json body;

    switch(event.reason)
    {
        case ModuleNew:
            body["reason"] = "new";
            break;
        case ModuleChanged:
            body["reason"] = "changed";
            break;
        case ModuleRemoved:
            body["reason"] = "removed";
            break;
    }

    json &module = body["module"];
    module["id"] = event.module.id;
    module["name"] = event.module.name;
    module["path"] = event.module.path;

    switch(event.module.symbolStatus)
    {
        case SymbolsSkipped:
            module["symbolStatus"] = "Skipped loading symbols.";
            break;
        case SymbolsLoaded:
            module["symbolStatus"] = "Symbols loaded.";
            break;
        case SymbolsNotFound:
            module["symbolStatus"] = "Symbols not found.";
            break;
    }

    EmitEvent("module", body);
}

void VSCodeProtocol::EmitOutputEvent(OutputEvent event)
{
    LogFuncEntry();
    json body;

    switch(event.category)
    {
        case OutputConsole:
            body["category"] = "console";
            break;
        case OutputStdOut:
            body["category"] = "stdout";
            break;
        case OutputStdErr:
            body["category"] = "stderr";
            break;
    }

    body["output"] = event.output;

    EmitEvent("output", body);
}

void VSCodeProtocol::EmitBreakpointEvent(BreakpointEvent event)
{
    LogFuncEntry();
    json body;

    switch(event.reason)
    {
        case BreakpointNew:
            body["reason"] = "new";
            break;
        case BreakpointChanged:
            body["reason"] = "changed";
            break;
        case BreakpointRemoved:
            body["reason"] = "removed";
            break;
    }

    body["breakpoint"] = event.breakpoint;

    EmitEvent("breakpoint", body);
}

void VSCodeProtocol::EmitInitializedEvent()
{
    LogFuncEntry();
    EmitEvent("initialized", json::object());
}

void VSCodeProtocol::EmitCapabilitiesEvent()
{
    LogFuncEntry();

    json body = json::object();
    json capabilities = json::object();

    AddCapabilitiesTo(capabilities);

    body["capabilities"] = capabilities;

    EmitEvent("capabilities", body);
}

void VSCodeProtocol::Cleanup()
{

}

static string VSCodeSeq(uint64_t id) {
    return string("{\"seq\":" + std::to_string(id) + ",");
}

void VSCodeProtocol::EmitEvent(const std::string &name, const nlohmann::json &body)
{
    std::lock_guard<std::mutex> lock(m_outMutex);
    json response;
    response["type"] = "event";
    response["event"] = name;
    response["body"] = body;
    std::string output = response.dump();
    output = VSCodeSeq(m_seqCounter) + output.substr(1);
    ++m_seqCounter;

    std::cout << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
    std::cout.flush();
    Log(LOG_EVENT, output);
}

typedef std::function<HRESULT(
    const json &arguments,
    json &body)> CommandCallback;

void VSCodeProtocol::AddCapabilitiesTo(json &capabilities)
{
    capabilities["supportsConfigurationDoneRequest"] = true;
    capabilities["supportsFunctionBreakpoints"] = true;
    capabilities["supportsConditionalBreakpoints"] = true;
    capabilities["supportTerminateDebuggee"] = true;
    capabilities["supportsExceptionInfoRequest"] = true;
}

HRESULT VSCodeProtocol::HandleCommand(const std::string &command, const json &arguments, json &body)
{
    static std::unordered_map<std::string, CommandCallback> commands {
    { "initialize", [this](const json &arguments, json &body){

        EmitCapabilitiesEvent();

        m_debugger->Initialize();

        AddCapabilitiesTo(body);

        return S_OK;
    } },
    { "setExceptionBreakpoints", [this](const json &arguments, json &body) {
        vector<string> filters = arguments.value("filters", vector<string>());
        ExceptionBreakMode mode;

        namespace KW = VSCodeExceptionBreakModeKeyWord;

        for (unsigned i = 0; i < filters.size(); i++)
        {
            if (filters[i].compare(KW::ALL) == 0 ||
                filters[i].compare(KW::ALWAYS) == 0) {
                mode.setAll();
            }
            if (filters[i].compare(KW::USERUNHANDLED) == 0 ||
                filters[i].compare(KW::USERUNHANDLED_A) == 0) {
                mode.setUserUnhandled();
            }
            // Nothing to do for "unhandled"
            if (filters[i].compare(KW::NEVER) == 0) {
                mode.resetAll();
            }
        }

        const string globalExceptionBreakpoint = "*";
        uint32_t id;
        m_debugger->InsertExceptionBreakpoint(mode, globalExceptionBreakpoint, id);

        // TODO:
        // - implement options support. options not supported in
        // current vscode 1.31.1 with C# plugin 1.17.1
        // - use ExceptionBreakpointStorage type for support options feature
        body["supportsExceptionOptions"] = false;

        return S_OK;
    } },
    { "configurationDone", [this](const json &arguments, json &body){
        return m_debugger->ConfigurationDone();
    } },
    { "exceptionInfo", [this](const json &arguments, json &body) {
        int threadId = arguments.at("threadId");
        ExceptionInfoResponse exceptionResponse;
        if (!m_debugger->GetExceptionInfoResponse(threadId, exceptionResponse))
        {
            body["breakMode"] = exceptionResponse.getVSCodeBreakMode();
            body["exceptionId"] = exceptionResponse.exceptionId;
            body["description"] = exceptionResponse.description;
            body["details"] = getVSCode(exceptionResponse.details);
            // vsdbg extension
            // body["code"] = 0;
            return S_OK;
        }
        return E_FAIL;
    } },
    { "setBreakpoints", [this](const json &arguments, json &body){
        HRESULT Status;

        std::vector<SourceBreakpoint> srcBreakpoints;
        for (auto &b : arguments.at("breakpoints"))
            srcBreakpoints.emplace_back(b.at("line"), b.value("condition", std::string()));

        std::vector<Breakpoint> breakpoints;
        IfFailRet(m_debugger->SetBreakpoints(arguments.at("source").at("path"), srcBreakpoints, breakpoints));

        body["breakpoints"] = breakpoints;

        return S_OK;
    } },
    {"launch", [this](const json &arguments, json &body) {
        const string cwd = arguments.at("cwd").get<string>();
        map<string, string> env;
        try {
            env = arguments.at("env").get<map<string, string> >();
        }
        catch (std::exception &ex) {
            Logger::levelLog(LOG_INFO, "'%s'", ex.what());
            // If we catch inconsistent state on the interrupted reading
            env.clear();
        }
        if (!m_fileExec.empty()) {
            return m_debugger->Launch(m_fileExec, m_execArgs, env, cwd, arguments.value("stopAtEntry", false));
        }
        vector<string> args = arguments.value("args", vector<string>());
        args.insert(args.begin(), arguments.at("program").get<std::string>());
        return m_debugger->Launch("dotnet", args, env, cwd, arguments.value("stopAtEntry", false));
    } },
    { "threads", [this](const json &arguments, json &body){
        HRESULT Status;
        std::vector<Thread> threads;
        IfFailRet(m_debugger->GetThreads(threads));

        body["threads"] = threads;

        return S_OK;
    } },
    { "disconnect", [this](const json &arguments, json &body){
        auto terminateArgIter = arguments.find("terminateDebuggee");
        Debugger::DisconnectAction action;
        if (terminateArgIter == arguments.end())
            action = Debugger::DisconnectDefault;
        else
            action = terminateArgIter.value().get<bool>() ? Debugger::DisconnectTerminate : Debugger::DisconnectDetach;

        m_debugger->Disconnect(action);

        m_exit = true;
        return S_OK;
    } },
    { "stackTrace", [this](const json &arguments, json &body){
        HRESULT Status;

        int totalFrames = 0;
        int threadId = arguments.at("threadId");

        std::vector<StackFrame> stackFrames;
        IfFailRet(m_debugger->GetStackTrace(
            threadId,
            arguments.value("startFrame", 0),
            arguments.value("levels", 0),
            stackFrames,
            totalFrames
            ));

        body["stackFrames"] = stackFrames;
        body["totalFrames"] = totalFrames;

        return S_OK;
    } },
    { "continue", [this](const json &arguments, json &body){
        body["allThreadsContinued"] = true;

        const int threadId = arguments.at("threadId");
        body["threadId"] = threadId;
        return m_debugger->Continue(threadId);
    } },
    { "pause", [this](const json &arguments, json &body){
        return m_debugger->Pause();
    } },
    { "next", [this](const json &arguments, json &body){
        return m_debugger->StepCommand(arguments.at("threadId"), Debugger::STEP_OVER);
    } },
    { "stepIn", [this](const json &arguments, json &body){
        return m_debugger->StepCommand(arguments.at("threadId"), Debugger::STEP_IN);
    } },
    { "stepOut", [this](const json &arguments, json &body){
        return m_debugger->StepCommand(arguments.at("threadId"), Debugger::STEP_OUT);
    } },
    { "scopes", [this](const json &arguments, json &body){
        HRESULT Status;
        std::vector<Scope> scopes;
        IfFailRet(m_debugger->GetScopes(arguments.at("frameId"), scopes));

        body["scopes"] = scopes;

        return S_OK;
    } },
    { "variables", [this](const json &arguments, json &body){
        HRESULT Status;

       std::string filterName = arguments.value("filter", "");
        VariablesFilter filter = VariablesBoth;
        if (filterName == "named")
            filter = VariablesNamed;
        else if (filterName == "indexed")
            filter = VariablesIndexed;

        std::vector<Variable> variables;
        IfFailRet(m_debugger->GetVariables(
            arguments.at("variablesReference"),
            filter,
            arguments.value("start", 0),
            arguments.value("count", 0),
            variables));

        body["variables"] = variables;

        return S_OK;
    } },
    { "evaluate", [this](const json &arguments, json &body){
        HRESULT Status;
        std::string expression = arguments.at("expression");
        uint64_t frameId;
        auto frameIdIter = arguments.find("frameId");
        if (frameIdIter == arguments.end())
        {
            int threadId = m_debugger->GetLastStoppedThreadId();
            frameId = StackFrame(threadId, 0, "").id;
        }
        else
            frameId = frameIdIter.value();

        // NOTE
        // VSCode don't support evaluation flags, we can't disable implicit function calls during evaluation.
        // https://github.com/OmniSharp/omnisharp-vscode/issues/3173
        Variable variable;
        std::string output;
        Status = m_debugger->Evaluate(frameId, expression, variable, output);
        if (FAILED(Status))
        {
            body["message"] = output;
            return Status;
        }

        body["result"] = variable.value;
        body["type"] = variable.type;
        body["variablesReference"] = variable.variablesReference;
        if (variable.variablesReference > 0)
        {
            body["namedVariables"] = variable.namedVariables;
            // indexedVariables
        }
        return S_OK;
    } },
    { "attach", [this](const json &arguments, json &body){
        int processId;

        const json &processIdArg = arguments.at("processId");
        if (processIdArg.is_string())
            processId = std::stoi(processIdArg.get<std::string>());
        else if (processIdArg.is_number())
            processId = processIdArg;
        else
            return E_INVALIDARG;

        return m_debugger->Attach(processId);
    } },
    { "setVariable", [this](const json &arguments, json &body) {
        HRESULT Status;

        std::string name = arguments.at("name");
        std::string value = arguments.at("value");
        int ref = arguments.at("variablesReference");

        std::string output;
        Status = m_debugger->SetVariable(name, value, ref, output);
        if (FAILED(Status))
        {
            body["message"] = output;
            return Status;
        }

        body["value"] = output;

        return S_OK;
    } },
    { "setFunctionBreakpoints", [this](const json &arguments, json &body) {
        HRESULT Status = S_OK;

        std::vector<FunctionBreakpoint> funcBreakpoints;
        for (auto &b : arguments.at("breakpoints"))
        {
            std::string module("");
            std::string params("");
            std::string name = b.at("name");

            std::size_t i = name.find('!');

            if (i != std::string::npos)
            {
                module = std::string(name, 0, i);
                name.erase(0, i + 1);
            }

            i = name.find('(');
            if (i != std::string::npos)
            {
                std::size_t closeBrace = name.find(')');

                params = std::string(name, i, closeBrace - i + 1);
                name.erase(i, closeBrace);
            }

            funcBreakpoints.emplace_back(module, name, params, b.value("condition", std::string()));
        }

        std::vector<Breakpoint> breakpoints;
        IfFailRet(m_debugger->SetFunctionBreakpoints(funcBreakpoints, breakpoints));

        body["breakpoints"] = breakpoints;

        return Status;
    } }
    };

    auto command_it = commands.find(command);
    if (command_it == commands.end())
    {
        return E_NOTIMPL;
    }

    return command_it->second(arguments, body);
}

const std::string VSCodeProtocol::TWO_CRLF("\r\n\r\n");
const std::string VSCodeProtocol::CONTENT_LENGTH("Content-Length: ");

std::string VSCodeProtocol::ReadData()
{
    std::string header;

    char c;
    while (true)
    {
        // Read until "\r\n\r\n"
        if (!std::cin.get(c))
            return std::string();

        header += c;
        if (header.length() < TWO_CRLF.length())
            continue;

        if (header.compare(header.length() - TWO_CRLF.length(), TWO_CRLF.length(), TWO_CRLF) != 0)
            continue;

        // Extract Content-Length
        auto lengthIndex = header.find(CONTENT_LENGTH);
        if (lengthIndex == std::string::npos)
            continue;

        size_t contentLength = std::stoul(header.substr(lengthIndex + CONTENT_LENGTH.length()));

        std::string result(contentLength, ' ');
        if (!std::cin.read(&result[0], contentLength))
            return std::string();

        return result;
    }
    // unreachable
}

void VSCodeProtocol::CommandLoop()
{
    while (!m_exit)
    {

        std::string requestText = ReadData();
        if (requestText.empty())
            break;

        {
            std::lock_guard<std::mutex> lock(m_outMutex);
            Log(LOG_COMMAND, requestText);
        }

        json request = json::parse(requestText);

        std::string command = request.at("command");
        // assert(request["type"] == "request");

        auto argIter = request.find("arguments");
        json arguments = (argIter == request.end() ? json::object() : argIter.value());

        json body = json::object();
        HRESULT Status = HandleCommand(command, arguments, body);

        {
            std::lock_guard<std::mutex> lock(m_outMutex);

            json response;

            response["type"] = "response";
            response["command"] = command;
            response["request_seq"] = request.at("seq");
            if (SUCCEEDED(Status))
            {
                response["success"] = true;
                response["body"] = body;
            }
            else
            {
                if (body.find("message") == body.end())
                {
                    std::ostringstream ss;
                    ss << "Failed command '" << command << "' : "
                    << "0x" << std::setw(8) << std::setfill('0') << std::hex << Status;
                    response["message"] = ss.str();
                }
                else
                    response["message"] = body["message"];

                response["success"] = false;
            }
            std::string output = response.dump();
            output = VSCodeSeq(m_seqCounter) + output.substr(1);
            ++m_seqCounter;

            std::cout << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
            std::cout.flush();
            Log(LOG_RESPONSE, output);
        }
    }

    if (!m_exit)
        m_debugger->Disconnect();

}

const std::string VSCodeProtocol::LOG_COMMAND("-> (C) ");
const std::string VSCodeProtocol::LOG_RESPONSE("<- (R) ");
const std::string VSCodeProtocol::LOG_EVENT("<- (E) ");

void VSCodeProtocol::EngineLogging(const std::string &path)
{
    if (path.empty())
    {
        m_engineLogOutput = LogConsole;
    }
    else
    {
        m_engineLogOutput = LogFile;
        m_engineLog.open(path);
    }
}

void VSCodeProtocol::Log(const std::string &prefix, const std::string &text)
{
    // Calling function must lock m_outMutex
    switch(m_engineLogOutput)
    {
        case LogNone:
            return;
        case LogFile:
            m_engineLog << prefix << text << std::endl;
            m_engineLog.flush();
            return;
        case LogConsole:
        {
            json response;
            response["type"] = "event";
            response["event"] = "output";
            response["body"] = json{
                {"category", "console"},
                {"output", prefix + text + "\n"}
            };
            std::string output = response.dump();
            output = VSCodeSeq(m_seqCounter) + output.substr(1);
            ++m_seqCounter;
            std::cout << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
            std::cout.flush();
            return;
        }
    }
}

string ExceptionInfoResponse::getVSCodeBreakMode() const
{
    namespace KW = VSCodeExceptionBreakModeKeyWord;

    if (breakMode.Never())
        return KW::NEVER;

    if (breakMode.All())
        return KW::ALWAYS;

    if (breakMode.OnlyUnhandled())
        return KW::UNHANDLED;

    // Throw() not supported for VSCode
    //  - description of "always: always breaks".
    // if (breakMode.Throw())

    if (breakMode.UserUnhandled())
        return KW::USERUNHANDLED;

    // Logical Error
    return "undefined";
}
