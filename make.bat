@ECHO OFF
REM Build the librx library and all supporting programs on Windows.
REM make clean will cleanup a previous build.

SET CFLAGS=
SET /A COUNT=0
SET TIME1=%TIME%

IF "%1"=="clean" (SET OK=) ELSE (GOTO :ENDIF)
    DIR /B /OD *.obj *.dll *.exe *.exp *.lib example6b.c > C:\temp\makeclean.txt 2>NUL
    FOR /F %%F IN (C:\temp\makeclean.txt) DO CALL :PRINT_DEL_FILE %%F
    IF %COUNT%==0 ECHO Nothing to clean.
    EXIT /B
:ENDIF

CALL :SETUP_VC_ENVIRONMENT
IF NOT DEFINED RESULT EXIT /B

CALL :NEEDS_UPDATE librx.dll rx.c rx.h hash.c hash.h
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% rx.c hash.c /link /DLL /DEF:librx.def /OUT:librx.dll
:ENDIF

CALL :NEEDS_UPDATE test.exe test.c librx.lib
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% test.c librx.lib
:ENDIF

CALL :NEEDS_UPDATE example1.exe example1.c librx.lib
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% example1.c librx.lib
:ENDIF

CALL :NEEDS_UPDATE example2.exe example2.c librx.lib
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% example2.c librx.lib
:ENDIF

CALL :NEEDS_UPDATE example3.exe example3.c librx.lib
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% example3.c librx.lib
:ENDIF

CALL :NEEDS_UPDATE example4.exe example4.c librx.lib
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% example4.c librx.lib setargv.obj
:ENDIF

CALL :NEEDS_UPDATE example5.exe example5.c librx.lib
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% example5.c librx.lib setargv.obj
:ENDIF

CALL :NEEDS_UPDATE example6.exe example6.c hash.c librx.lib
IF NOT DEFINED RESULT GOTO :ENDIF
    CALL :DO CL %CFLAGS% example6.c hash.c librx.lib setargv.obj
	CALL :DO .\example6 example6b.lx
    CALL :DO CL %CFLAGS% example6b.c librx.lib setargv.obj
:ENDIF

SET TIME2=%TIME%
IF %COUNT%==0 (GOTO :THEN) ELSE (GOTO :ELSE)
:THEN
    ECHO Everything up to date.
    GOTO :ENDIF
:ELSE
    CALL :TIME_DIFF %TIME1% %TIME2%
    ECHO Made %COUNT% Things in %RESULT%.
:ENDIF

EXIT /B




REM Finds the %1 in %PATH%, returns the result in %RESULT%
:FIND_IN_PATH
    SET RESULT=%~$PATH:1
EXIT /B

REM Determines if %1 needs to be updated, such as if it does not exist,
REM or if it is older than %2, %3, %4, etc.
:NEEDS_UPDATE
    SET TARGET=%~1
    IF NOT EXIST "%TARGET%" GOTO :YES_NEEDS_UPDATE
    FOR /F "tokens=1,2" %%F IN ('DIR /B /O-D %*') DO SET NEWEST=%%~F& GOTO :OUT
    :OUT
    IF NOT "%TARGET%"=="%NEWEST%" GOTO :YES_NEEDS_UPDATE
    SET RESULT=
    EXIT /B
    :YES_NEEDS_UPDATE
    SET RESULT=YES
    SET /A COUNT+=1
EXIT /B

REM Find CL.exe, if not found run vcvars32.bat, if still not found, exit.
:SETUP_VC_ENVIRONMENT
    CALL :FIND_IN_PATH CL.exe
    IF NOT DEFINED RESULT GOTO :NO_CL_1
    GOTO :YES_CL
    :NO_CL_1
        CALL :FIND_IN_PATH vcvars32.bat
        IF NOT DEFINED RESULT (GOTO :NO_VCVARS32) ELSE (GOTO :YES_VCVARS32)
        :NO_VCVARS32
            ECHO Did not find the CL program. Please run vcvars32.bat.
            EXIT /B
        :YES_VCVARS32
            ECHO Running vcvars32.bat
            CALL vcvars32.bat
            CALL :FIND_IN_PATH CL.exe
            IF NOT DEFINED RESULT (GOTO :NO_CL_2) ELSE (GOTO :YES_CL)
            :NO_CL_2
                ECHO Did not find the CL program. Please run vcvars32.bat
                EXIT /B
    :YES_CL
    SET RESULT=YES
EXIT /B

REM Prints and then deletes the file given to it.
:PRINT_DEL_FILE
    SET "SIZE=%~z1          "
    SET SIZE=%SIZE:~0,10%
    ECHO %~t1 %SIZE% %~1
    DEL %1
    SET /A COUNT+=1
EXIT /B

REM This will print the command before it does it.
:DO
    ECHO [1;36m%*[0m
    %*
    ECHO:
EXIT /B

REM Calculates the difference in seconds between two timestamps.
REM https://stackoverflow.com/questions/673523/how-do-i-measure-execution-time-of-a-command-on-the-windows-command-line#6209392
:TIME_DIFF
    SET OPTIONS="tokens=1-4 delims=:.,"
    FOR /F %OPTIONS% %%A in ("%1") DO SET H1=%%A&SET /A M1=100%%B %% 100&SET /A S1=100%%C %% 100&SET /A MS1=100%%D %% 100
    FOR /F %OPTIONS% %%A in ("%2") DO SET H2=%%A&SET /A M2=100%%B %% 100&SET /A S2=100%%C %% 100&SET /A MS2=100%%D %% 100
    SET /A H3=%H2%-%H1%
    SET /A M3=%M2%-%M1%
    SET /A S3=%S2%-%S1%
    SET /A MS3=%MS2%-%MS1%
    IF %MS3% lss 0 SET /A S3 = %S3% - 1 & SET /A MS3 = 100%MS3%
    IF %S3% lss 0 SET /A M3 = %M3% - 1 & SET /A S3 = 60%S3%
    IF %M3% lss 0 SET /A H3 = %H3% - 1 & SET /A M3 = 60%M3%
    IF %H3% lss 0 SET /A H3 = 24%H3%
    IF 1%MS3% lss 100 SET MS3=0%MS3%
    SET /A S4 = %H3%*3600 + %M3%*60 + %S3%
    REM ECHO command took %H3%:%M3%:%S3%.%MS3% (%S4%.%MS3%s total)
    SET RESULT=%S4%.%MS3% seconds
EXIT /B

