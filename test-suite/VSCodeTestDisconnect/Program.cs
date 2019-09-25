using System;
using System.IO;
using System.Collections.Generic;

using NetcoreDbgTest;
using NetcoreDbgTest.VSCode;
using NetcoreDbgTest.Script;

using Xunit;
using Newtonsoft.Json;

namespace NetcoreDbgTest.Script
{
    class Context
    {
        public static void PrepareStart()
        {
            InitializeRequest initializeRequest = new InitializeRequest();
            initializeRequest.arguments.clientID = "vscode";
            initializeRequest.arguments.clientName = "Visual Studio Code";
            initializeRequest.arguments.adapterID = "coreclr";
            initializeRequest.arguments.pathFormat = "path";
            initializeRequest.arguments.linesStartAt1 = true;
            initializeRequest.arguments.columnsStartAt1 = true;
            initializeRequest.arguments.supportsVariableType = true;
            initializeRequest.arguments.supportsVariablePaging = true;
            initializeRequest.arguments.supportsRunInTerminalRequest = true;
            initializeRequest.arguments.locale = "en-us";
            Assert.True(VSCodeDebugger.Request(initializeRequest).Success);

            LaunchRequest launchRequest = new LaunchRequest();
            launchRequest.arguments.name = ".NET Core Launch (console) with pipeline";
            launchRequest.arguments.type = "coreclr";
            launchRequest.arguments.preLaunchTask = "build";
            launchRequest.arguments.program = DebuggeeInfo.TargetAssemblyPath;
            // NOTE this code works only with one source file
            launchRequest.arguments.cwd = Directory.GetParent(DebuggeeInfo.SourceFilesPath).FullName;
            launchRequest.arguments.console = "internalConsole";
            launchRequest.arguments.stopAtEntry = true;
            launchRequest.arguments.internalConsoleOptions = "openOnSessionStart";
            launchRequest.arguments.__sessionId = Guid.NewGuid().ToString();
            Assert.True(VSCodeDebugger.Request(launchRequest).Success);
        }

        public static void PrepareEnd()
        {
            ConfigurationDoneRequest configurationDoneRequest = new ConfigurationDoneRequest();
            Assert.True(VSCodeDebugger.Request(configurationDoneRequest).Success);
        }

        public static void WasEntryPointHit()
        {
            string resJSON = VSCodeDebugger.Receive(-1);
            Assert.True(VSCodeDebugger.isResponseContainProperty(resJSON, "event", "stopped")
                        && VSCodeDebugger.isResponseContainProperty(resJSON, "reason", "entry"));

            foreach (var Event in VSCodeDebugger.EventList) {
                if (VSCodeDebugger.isResponseContainProperty(Event, "event", "stopped")
                    && VSCodeDebugger.isResponseContainProperty(Event, "reason", "entry")) {
                    threadId = Convert.ToInt32(VSCodeDebugger.GetResponsePropertyValue(Event, "threadId"));
                    break;
                }
            }
        }

        public static void Continue()
        {
            ContinueRequest continueRequest = new ContinueRequest();
            continueRequest.arguments.threadId = threadId;
            Assert.True(VSCodeDebugger.Request(continueRequest).Success);
        }

        public static void AddBreakpoint(string bpName, string Condition = null)
        {
            Breakpoint bp = DebuggeeInfo.Breakpoints[bpName];
            Assert.Equal(BreakpointType.Line, bp.Type);
            var lbp = (LineBreakpoint)bp;

            BreakpointSourceName = lbp.FileName;
            BreakpointList.Add(new SourceBreakpoint(lbp.NumLine, Condition));
            BreakpointLines.Add(lbp.NumLine);
        }

        public static void SetBreakpoints()
        {
            SetBreakpointsRequest setBreakpointsRequest = new SetBreakpointsRequest();
            setBreakpointsRequest.arguments.source.name = BreakpointSourceName;
            // NOTE this code works only with one source file
            setBreakpointsRequest.arguments.source.path = DebuggeeInfo.SourceFilesPath;
            setBreakpointsRequest.arguments.lines.AddRange(BreakpointLines);
            setBreakpointsRequest.arguments.breakpoints.AddRange(BreakpointList);
            setBreakpointsRequest.arguments.sourceModified = false;
            Assert.True(VSCodeDebugger.Request(setBreakpointsRequest).Success);
        }

        public static void WasExit()
        {
            bool wasExited = false;
            bool wasTerminated = false;
            foreach (var Event in VSCodeDebugger.EventList) {
                if (VSCodeDebugger.isResponseContainProperty(Event, "event", "exited")) {
                    wasExited = true;
                }
                if (VSCodeDebugger.isResponseContainProperty(Event, "event", "terminated")) {
                    wasTerminated = true;
                }
            }
            Assert.True(wasExited && wasTerminated);
        }

        public static void DebuggerExit()
        {
            DisconnectRequest disconnectRequest = new DisconnectRequest();
            disconnectRequest.arguments = new DisconnectArguments();
            disconnectRequest.arguments.restart = false;
            Assert.True(VSCodeDebugger.Request(disconnectRequest).Success);
        }

        static VSCodeDebugger VSCodeDebugger = new VSCodeDebugger();
        static int threadId = -1;
        public static string BreakpointSourceName;
        public static List<SourceBreakpoint> BreakpointList = new List<SourceBreakpoint>();
        public static List<int> BreakpointLines = new List<int>();
    }
}

namespace VSCodeTestDisconnect
{
    class Program
    {
        static void Main(string[] args)
        {
            Label.Checkpoint("init", "finish", () => {
                Context.PrepareStart();
                Context.AddBreakpoint("bp");
                Context.SetBreakpoints();
                Context.PrepareEnd();
                Context.WasEntryPointHit();
                Context.Continue();
            });

            Label.Checkpoint("finish", "", () => {;
                Context.DebuggerExit();
                Context.WasExit();
            });

            System.Threading.Thread.Sleep(30000);
            // we should never reach this code
            Console.WriteLine("A breakpoint \"bp\" is set on this line"); Label.Breakpoint("bp");
        }
    }
}
