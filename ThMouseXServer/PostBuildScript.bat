call "%DevEnvDir%..\Tools\vsdevcmd.bat" -no_logo
set PostBuildTool="%SolutionDir%PostBuildTool\bin\x86\%Configuration%\net48\PostBuildTool"
set ServerName=ThMouseXServer
set ClientName=ThMouseX
set TypeLibName=ThMouseXServer.dll
set Architecture=*
set ServerPath="%ProjectDir%%OutDir%ThMouseXServer.dll"
set ServerManifestPath=server.manifest
set ClientManifestPath=client.manifest
set TlbPath=server.tlb
rem Make a manifest file for the Server
%PostBuildTool% GenerateComServerManifest %ServerName% %TypeLibName% %Architecture% %ServerPath% %ServerManifestPath%
   if %errorlevel% neq 0 exit /b %errorlevel%
rem Make a manifest file for the client
%PostBuildTool% GenerateComServerManifest %ClientName% %TypeLibName% %Architecture% %ServerPath% %ClientManifestPath%
   if %errorlevel% neq 0 exit /b %errorlevel%
rem Generate a TypeLib file
tlbexp /nologo /win32 %ServerPath% /out:%TlbPath%
   if %errorlevel% neq 0 exit /b %errorlevel%
rem Embed the Typelib file into the Server dll file
%PostBuildTool% ImportResource %ServerPath% %TlbPath% typelib #1
   if %errorlevel% neq 0 exit /b %errorlevel%
if not exist AutoGenerated mkdir AutoGenerated
cd AutoGenerated
rem Update the tlh and tli only if their content changes
ren server.tlh old_server.tlh > nul 2> nul
ren server.tli old_server.tli > nul 2> nul
cl -nologo "%ProjectDir%GenerateComHeaders.cpp"
fc server.tlh old_server.tlh > nul 2> nul
if %errorlevel% neq 0 (
	rem There are changes
	del old_server.tlh > nul 2> nul
	del old_server.tli > nul 2> nul
) else (
	rem No change
	del server.tlh
	del server.tli
	ren old_server.tlh server.tlh > nul
	ren old_server.tli server.tli > nul
)
rem Remove trash
del GenerateComHeaders.exe
del GenerateComHeaders.obj
cd ..