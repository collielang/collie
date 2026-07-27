@echo off
rem 用法：time_run.bat <collie 源文件相对路径>
rem 打印运行前后的系统时间，用于粗略计时（避开外层 shell 引号问题）
cd /d "%~dp0..\.."
echo START %time%
compiler\build\Release\collie.exe %1
echo EXITCODE %errorlevel%
echo END %time%
