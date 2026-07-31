@echo off
rem Arguments: %%1=event  %%2=peer_id  %%3=detail
setlocal
set "AIRAN_EVENT=%~1"
set "AIRAN_PEER_ID=%~2"
set "AIRAN_DETAIL=%~3"

eventcreate /T INFORMATION /ID 100 /L APPLICATION /SO Airan-Desk /D "Airan-Desk: %AIRAN_EVENT% from %AIRAN_PEER_ID% - %AIRAN_DETAIL%" >nul 2>&1
if errorlevel 1 echo Airan-Desk: %AIRAN_EVENT% from %AIRAN_PEER_ID% - %AIRAN_DETAIL%

endlocal
