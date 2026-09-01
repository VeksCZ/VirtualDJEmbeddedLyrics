Option Explicit

' Launch without a console window. A folder may be passed from Explorer or by
' drag-and-drop; otherwise the current working directory is used.

Dim shell, args, fso, scriptDir, pyScript, targetDir, requestedTab, command, status, requirements
Set shell = CreateObject("WScript.Shell")
Set args = WScript.Arguments
Set fso = CreateObject("Scripting.FileSystemObject")

scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
pyScript = fso.BuildPath(scriptDir, "lyrics_tools_gui.py")
If Not fso.FileExists(pyScript) Then
    MsgBox "lyrics_tools_gui.py was not found next to this launcher.", vbCritical, "MP3 & Lyrics Tools"
    WScript.Quit 1
End If
If args.Count > 1 Then
    requestedTab = LCase(args(1))
Else
    requestedTab = ""
End If

requirements = fso.BuildPath(scriptDir, "requirements.txt")
If Not fso.FileExists(requirements) Then
    requirements = fso.BuildPath(scriptDir, "..\requirements.txt")
End If

If args.Count > 0 Then
    targetDir = fso.GetAbsolutePathName(args(0))
Else
    targetDir = shell.CurrentDirectory
End If

On Error Resume Next
status = shell.Run("pythonw -c ""import tkinter, mutagen, tidalapi""", 0, True)
If Err.Number <> 0 Or status <> 0 Then
    Err.Clear
    MsgBox "Python or required packages were not found." & vbCrLf & vbCrLf & _
           "Run this command first:" & vbCrLf & _
           "python -m pip install -r """ & requirements & """", _
           vbExclamation, "MP3 & Lyrics Tools"
    WScript.Quit 1
End If

command = "pythonw """ & pyScript & """ """ & targetDir & """"
If requestedTab <> "" Then command = command & " --tab """ & requestedTab & """"
status = shell.Run(command, 0, True)
If Err.Number <> 0 Or status <> 0 Then
    MsgBox "MP3 & Lyrics Tools could not be started or exited with an error." & vbCrLf & _
           "Use MP3 & Lyrics Tools.cmd to see detailed diagnostics.", _
           vbExclamation, "MP3 & Lyrics Tools"
    WScript.Quit 1
End If

WScript.Quit 0
