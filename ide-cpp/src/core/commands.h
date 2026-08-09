#pragma once

// Command IDs — 메뉴/단축키/팔레트 공통. 기존 Win32 IDC_* 와 대응.
enum CommandId {
    Cmd_New = 100, Cmd_Open, Cmd_Save, Cmd_SaveAs, Cmd_Run, Cmd_Stop, Cmd_Clear,
    Cmd_OpenFolder, Cmd_CloseFolder, Cmd_SetDefaultTerminalDir,
    Cmd_ThemeToggle,
    Cmd_DetachIner, Cmd_DetachOuter, Cmd_DetachTree,
    Cmd_ToggleSidebar, Cmd_TogglePanel, Cmd_ToggleIner, Cmd_ResetLayout,
    Cmd_NewFile, Cmd_NewFolder, Cmd_Refresh,
    Cmd_Rename, Cmd_Delete,
    Cmd_QuickFiles, Cmd_QuickCmds,
    Cmd_NextTab, Cmd_PrevTab, Cmd_CloseTab, Cmd_CloseOthers, Cmd_CloseAll,
    Cmd_Find, Cmd_FindNext, Cmd_FindPrev, Cmd_Replace, Cmd_GoToLine, Cmd_Comment,
    Cmd_ZoomIn, Cmd_ZoomOut, Cmd_ZoomReset,
    Cmd_Quit
};
