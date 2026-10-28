Scriptname SKI_ConfigBase extends SKI_QuestBase Hidden
{Stub de compilação: só as assinaturas públicas do MCM do SkyUI (API 4+, SkyUI 5.x) usadas pelo
 AgeOfSkyrimMCM, escrito à mão. NÃO distribuir: em jogo vale o SKI_ConfigBase.pex instalado pelo SkyUI.}

Int Property OPTION_FLAG_NONE = 0x00 AutoReadOnly
Int Property OPTION_FLAG_DISABLED = 0x01 AutoReadOnly
Int Property LEFT_TO_RIGHT = 1 AutoReadOnly
Int Property TOP_TO_BOTTOM = 2 AutoReadOnly

String Property ModName Auto
String[] Property Pages Auto
String Property CurrentPage
	String Function Get()
		Return ""
	EndFunction
EndProperty

; ---- Eventos ----
Event OnConfigInit()
EndEvent
Event OnConfigOpen()
EndEvent
Event OnConfigClose()
EndEvent
Event OnPageReset(String a_page)
EndEvent
Event OnOptionHighlight(Int a_option)
EndEvent
Event OnOptionSelect(Int a_option)
EndEvent
Event OnOptionDefault(Int a_option)
EndEvent
Event OnOptionMenuOpen(Int a_option)
EndEvent
Event OnOptionMenuAccept(Int a_option, Int a_index)
EndEvent
Event OnOptionColorOpen(Int a_option)
EndEvent
Event OnOptionColorAccept(Int a_option, Int a_color)
EndEvent
Event OnOptionInputOpen(Int a_option)
EndEvent
Event OnOptionInputAccept(Int a_option, String a_input)
EndEvent

; ---- Funções ----
Function ForcePageReset()
EndFunction
Function SetInfoText(String a_text)
EndFunction
Function SetCursorPosition(Int a_position)
EndFunction
Function SetCursorFillMode(Int a_fillMode)
EndFunction
Int Function AddEmptyOption()
	Return -1
EndFunction
Int Function AddHeaderOption(String a_text, Int a_flags = 0)
	Return -1
EndFunction
Int Function AddTextOption(String a_text, String a_value, Int a_flags = 0)
	Return -1
EndFunction
Int Function AddMenuOption(String a_text, String a_value, Int a_flags = 0)
	Return -1
EndFunction
Int Function AddColorOption(String a_text, Int a_color, Int a_flags = 0)
	Return -1
EndFunction
Int Function AddInputOption(String a_text, String a_value, Int a_flags = 0)
	Return -1
EndFunction
Function SetTextOptionValue(Int a_option, String a_value, Bool a_noUpdate = False)
EndFunction
Function SetMenuOptionValue(Int a_option, String a_value, Bool a_noUpdate = False)
EndFunction
Function SetColorOptionValue(Int a_option, Int a_color, Bool a_noUpdate = False)
EndFunction
Function SetInputOptionValue(Int a_option, String a_value, Bool a_noUpdate = False)
EndFunction
Function SetMenuDialogStartIndex(Int a_value)
EndFunction
Function SetMenuDialogDefaultIndex(Int a_value)
EndFunction
Function SetMenuDialogOptions(String[] a_options)
EndFunction
Function SetColorDialogStartColor(Int a_color)
EndFunction
Function SetColorDialogDefaultColor(Int a_color)
EndFunction
Function SetInputDialogStartText(String a_text)
EndFunction
Bool Function ShowMessage(String a_message, Bool a_withCancel = True, String a_acceptLabel = "$Accept", String a_cancelLabel = "$Cancel")
	Return True
EndFunction
