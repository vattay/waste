!define APP_NAME_BIG "WASTE"
!define APP_NAME_SMALL "WASTE" ; used for directory name and registry
!define APP_EXENAME "waste.exe"
OutFile waste-setup.exe

Name "${APP_NAME_BIG}"
Caption "${APP_NAME_BIG} - Setup"

BGGradient 000000 308030 FFFFFF
InstallColors FF8080 000000
InstProgressFlags smooth colored

LicenseText "WASTE is free software. Please agree to the license below to continue."
LicenseData license.txt

ComponentText "This will install ${APP_NAME_BIG}"
AutoCloseWindow true
ShowInstDetails show
ShowUninstDetails show
DirText "Please select a location to install ${APP_NAME_BIG} (or use the default):"
SetOverwrite on
SetDateSave on
!ifdef HAVE_UPX
  !packhdr tmp.dat "upx --force --best --compress-icons=1 tmp.dat"
!endif

InstallDir $PROGRAMFILES\${APP_NAME_SMALL}
InstallDirRegKey HKLM SOFTWARE\${APP_NAME_SMALL} ""

Function .onInit
  ReadRegStr $0 HKLM SOFTWARE\${APP_NAME_BIG} ""
  StrCmp $0 "" end
   IfFileExists $SMPROGRAMS\${APP_NAME_BIG}\*.* smthere
     SectionSetFlags 1 0
   smthere:
end:
   IfFileExists $SMSTARTUP\${APP_NAME_BIG}.lnk slthere ; by default do not do startup
     SectionSetFlags 2 0
   slthere:

FunctionEnd

Section "${APP_NAME_BIG} (required)"
  SetOutPath $INSTDIR
  File release\${APP_EXENAME}
  File license.txt
  CreateDirectory $INSTDIR\Downloads
SectionEnd

Section "Start Menu shortcuts"
  CreateDirectory $SMPROGRAMS\${APP_NAME_BIG}
  CreateShortCut "$SMPROGRAMS\${APP_NAME_BIG}\Uninstall ${APP_NAME_BIG}.lnk" \
                 "$INSTDIR\uninst.exe"
  CreateShortCut "$SMPROGRAMS\${APP_NAME_BIG}\${APP_NAME_BIG}.lnk" \
                 "$INSTDIR\${APP_EXENAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME_BIG}\${APP_NAME_BIG} License.lnk" \
                 "$INSTDIR\license.txt"
  CreateShortCut "$SMPROGRAMS\${APP_NAME_BIG}\${APP_NAME_BIG} Downloads Directory.lnk" \
                 "$INSTDIR\downloads"
SectionEnd

Section "Launch on startup"
  CreateShortCut "$SMSTARTUP\${APP_NAME_BIG}.lnk" \
                 "$INSTDIR\${APP_EXENAME}"
SectionEnd

Section -post

  WriteRegStr HKLM SOFTWARE\${APP_NAME_SMALL} "" $INSTDIR
  WriteRegStr HKCR waste "" "URL: WASTE Command Protocol"
  WriteRegStr HKCR waste "URL Protocol" ""
  WriteRegStr HKCR .wastestate "" "WASTESTATE"
  WriteRegStr HKCR WASTESTATE "" "WASTE transfer information file"
  WriteRegStr HKCR WASTESTATE\DefaultIcon "" "$INSTDIR\WASTE.exe,1"
  WriteRegStr HKCR waste\shell\open\command "" '"$INSTDIR\waste.exe" "%1"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME_SMALL}" \
                   "DisplayName" "${APP_NAME_BIG} (remove only)"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME_SMALL}" \
                   "UninstallString" '"$INSTDIR\uninst.exe"'


  ; since the installer is now created last (in 1.2+), this makes sure 
  ; that any old installer that is readonly is overwritten.
  Delete $INSTDIR\uninst.exe 
  WriteUninstaller $INSTDIR\uninst.exe
SectionEnd

Function .onInstSuccess
  Exec '"$INSTDIR\${APP_EXENAME}"'
FunctionEnd

!ifndef NO_UNINST
UninstallText "This will uninstall ${APP_NAME_BIG} from your system:"

Section Uninstall
TryAgain:
  SetFileAttributes $INSTDIR\${APP_EXENAME} NORMAL
  Delete $INSTDIR\${APP_EXENAME}
  IfFileExists "$INSTDIR\${APP_EXENAME}" 0 DeletedEXE
    MessageBox MB_RETRYCANCEL|MB_ICONQUESTION "${APP_EXENAME} appears to be locked, please make sure ${APP_NAME_BIG} is not running before uninstalling." IDRETRY TryAgain
    Abort "${APP_EXENAME} could not be removed. ${APP_NAME_BIG} may have been running."
DeletedEXE:
  
  Delete $INSTDIR\readme.txt
  Delete $INSTDIR\uninst.exe
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME_SMALL}"
  DeleteRegKey HKLM "Software\${APP_NAME_SMALL}"
  DeleteRegKey HKCR waste
  DeleteRegKey HKCR .wastestate
  DeleteRegKey HKCR WASTESTATE
  RMDir /r $SMPROGRAMS\${APP_NAME_BIG}
  Delete $SMSTARTUP\${APP_NAME_BIG}.lnk

  RMDir $INSTDIR\Downloads

  IfFileExists $INSTDIR\downloads\*.* 0 NoDownloads

    MessageBox MB_YESNO|MB_ICONQUESTION "Remove any files you have downloaded (in $INSTDIR\downloads)?" IDNO NoDownloads
    Delete $INSTDIR\Downloads\*.*
    RMDir $INSTDIR\Downloads

NoDownloads:

  RMDir $INSTDIR
  
  MessageBox MB_YESNO|MB_ICONQUESTION "Remove network profiles from your ${APP_NAME_BIG} directory?" IDNO AllDone
    Delete $INSTDIR\*.*
    RMDir $INSTDIR
  AllDone:
SectionEnd

!endif
