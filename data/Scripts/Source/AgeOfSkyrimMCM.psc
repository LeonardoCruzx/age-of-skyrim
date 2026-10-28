Scriptname AgeOfSkyrimMCM extends SKI_ConfigBase
{Age of Skyrim: menu MCM (SkyUI). Casca fina: toda a lógica e todos os textos vêm do DLL
 (natives em AgeOfSkyrim.psc, textos no idioma do settings.json). Ver ADR 0033.}

; ---- Estado da tela (nada de lógica do jogo aqui) -----------------------------------------------
Int kTerritoriesPerPage = 40

Bool _dllOk = False
Int _terrPage = 0
String _newName = ""
String _newStyle = ""
Int _newColor = 0xC0C0C0

String[] _styles
String[] _terrKeys
Int[] _terrOpts
String[] _ownerChoices
Int _menuTerritory = -1

; Ids das opções da página atual (-1 = não existe nesta página)
Int _oName = -1
Int _oStyle = -1
Int _oColor = -1
Int _oCreate = -1
Int _oPrev = -1
Int _oNext = -1
Int _oSelfTest = -1
Int _oStock = -1
Int _oClaim = -1

; ---- SkyUI --------------------------------------------------------------------------------------
Int Function GetVersion()
	Return 1
EndFunction

Event OnConfigInit()
	ModName = "$AOS_ModName"
	BuildPages()
EndEvent

; Recriadas a cada abertura: seguem o idioma atual do settings.json e a presença do DLL.
Event OnConfigOpen()
	BuildPages()
EndEvent

Function BuildPages()
	_dllOk = AgeOfSkyrim.GetApiVersion() > 0
	If !_dllOk
		Pages = new String[1]
		Pages[0] = "$AOS_DllMissing"
		Return
	EndIf
	Pages = new String[5]
	Pages[0] = AgeOfSkyrim.Tr("mcm.page.faction")
	Pages[1] = AgeOfSkyrim.Tr("mcm.page.territories")
	Pages[2] = AgeOfSkyrim.Tr("mcm.page.stock")
	Pages[3] = AgeOfSkyrim.Tr("mcm.page.realm")
	Pages[4] = AgeOfSkyrim.Tr("mcm.page.debug")
EndFunction

Function ResetOptionIds()
	_oName = -1
	_oStyle = -1
	_oColor = -1
	_oCreate = -1
	_oPrev = -1
	_oNext = -1
	_oSelfTest = -1
	_oStock = -1
	_oClaim = -1
	_menuTerritory = -1
	_terrOpts = new Int[128]
	Int i = 0
	While i < 128
		_terrOpts[i] = -1
		i += 1
	EndWhile
EndFunction

Event OnPageReset(String a_page)
	ResetOptionIds()
	SetCursorFillMode(TOP_TO_BOTTOM)
	If !_dllOk
		AddTextOption("$AOS_DllMissing", "", OPTION_FLAG_DISABLED)
		Return
	EndIf
	If a_page == "" || a_page == Pages[0]
		PageFaction()
	ElseIf a_page == Pages[1]
		PageTerritories()
	ElseIf a_page == Pages[2]
		PageStock()
	ElseIf a_page == Pages[3]
		PageRealm()
	ElseIf a_page == Pages[4]
		PageDebug()
	EndIf
EndEvent

; ---- Páginas ------------------------------------------------------------------------------------
Function PageFaction()
	String mine = AgeOfSkyrim.GetPlayerFaction()
	_styles = AgeOfSkyrim.GetStyles()
	If mine == ""
		If _newStyle == "" && _styles.Length > 0
			_newStyle = _styles[0]
		EndIf
		AddHeaderOption(AgeOfSkyrim.Tr("mcm.faction.createHeader"))
		_oName = AddInputOption(AgeOfSkyrim.Tr("mcm.faction.name"), _newName)
		_oStyle = AddMenuOption(AgeOfSkyrim.Tr("mcm.faction.style"), AgeOfSkyrim.GetStyleName(_newStyle))
		_oColor = AddColorOption(AgeOfSkyrim.Tr("mcm.faction.color"), _newColor)
		Int flags = OPTION_FLAG_NONE
		If _newName == ""
			flags = OPTION_FLAG_DISABLED
		EndIf
		_oCreate = AddTextOption(AgeOfSkyrim.Tr("mcm.faction.create"), "", flags)
	Else
		AddHeaderOption(AgeOfSkyrim.Tr("mcm.faction.mineHeader"))
		_oName = AddInputOption(AgeOfSkyrim.Tr("mcm.faction.name"), AgeOfSkyrim.GetFactionName(mine))
		_oStyle = AddMenuOption(AgeOfSkyrim.Tr("mcm.faction.style"), AgeOfSkyrim.GetStyleName(AgeOfSkyrim.GetFactionStyle(mine)))
		_oColor = AddColorOption(AgeOfSkyrim.Tr("mcm.faction.color"), AgeOfSkyrim.GetFactionColor(mine))
		AddTextOption(AgeOfSkyrim.Tr("mcm.faction.owned"), AgeOfSkyrim.CountTerritoriesOwnedBy(mine) as String, OPTION_FLAG_DISABLED)
	EndIf

	SetCursorPosition(1)
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.faction.allHeader"))
	String[] all = AgeOfSkyrim.GetFactions()
	Int i = 0
	While i < all.Length
		AddTextOption(AgeOfSkyrim.GetFactionName(all[i]), AgeOfSkyrim.Tr1("mcm.faction.territories", AgeOfSkyrim.CountTerritoriesOwnedBy(all[i]) as String), OPTION_FLAG_DISABLED)
		i += 1
	EndWhile
EndFunction

Function PageTerritories()
	Int total = AgeOfSkyrim.GetTerritoryCount()
	Int pageCount = (total + kTerritoriesPerPage - 1) / kTerritoriesPerPage
	If pageCount < 1
		pageCount = 1
	EndIf
	If _terrPage >= pageCount
		_terrPage = 0
	EndIf

	SetCursorFillMode(LEFT_TO_RIGHT)
	Int prevFlags = OPTION_FLAG_NONE
	If _terrPage == 0
		prevFlags = OPTION_FLAG_DISABLED
	EndIf
	Int nextFlags = OPTION_FLAG_NONE
	If _terrPage >= pageCount - 1
		nextFlags = OPTION_FLAG_DISABLED
	EndIf
	_oPrev = AddTextOption(AgeOfSkyrim.Tr("mcm.terr.prev"), "", prevFlags)
	_oNext = AddTextOption(AgeOfSkyrim.Tr("mcm.terr.next"), "", nextFlags)
	AddHeaderOption(AgeOfSkyrim.Tr2("mcm.terr.page", (_terrPage + 1) as String, pageCount as String))
	AddHeaderOption(AgeOfSkyrim.Tr1("mcm.terr.total", total as String))

	; Com debug ligado o dono pode ser trocado por um menu; senão a lista é só leitura.
	Bool editable = AgeOfSkyrim.IsDebugEnabled()
	_terrKeys = AgeOfSkyrim.GetTerritories(_terrPage * kTerritoriesPerPage, kTerritoriesPerPage)
	Int i = 0
	While i < _terrKeys.Length
		String label = AgeOfSkyrim.GetTerritoryName(_terrKeys[i])
		String owner = AgeOfSkyrim.GetFactionName(AgeOfSkyrim.GetTerritoryOwner(_terrKeys[i]))
		If editable
			_terrOpts[i] = AddMenuOption(label, owner)
		Else
			_terrOpts[i] = AddTextOption(label, owner)
		EndIf
		i += 1
	EndWhile
EndFunction

Function PageStock()
	String mine = AgeOfSkyrim.GetPlayerFaction()
	String[] names = AgeOfSkyrim.GetStockCategoryNames()

	AddHeaderOption(AgeOfSkyrim.Tr("mcm.seat.header"))
	If mine != "" && AgeOfSkyrim.HasSeat(mine)
		AddTextOption(AgeOfSkyrim.Tr("mcm.seat.location"), AgeOfSkyrim.GetTerritoryName(AgeOfSkyrim.GetSeatTerritory(mine)), OPTION_FLAG_DISABLED)
		ObjectReference chest = AgeOfSkyrim.GetSeatChest(mine)
		If chest
			AddStockLines(names, AgeOfSkyrim.GetContainerStockTotals(chest))
		EndIf
	Else
		AddTextOption(AgeOfSkyrim.Tr("mcm.seat.none"), "", OPTION_FLAG_DISABLED)
	EndIf

	SetCursorPosition(1)
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.stock.playerHeader"))
	If names.Length == 0
		AddTextOption(AgeOfSkyrim.Tr("mcm.stock.disabled"), "", OPTION_FLAG_DISABLED)
	Else
		AddStockLines(names, AgeOfSkyrim.GetPlayerStockTotals())
	EndIf
EndFunction

; Uma linha por categoria + "sem categoria" (último elemento de totals).
Function AddStockLines(String[] names, Int[] totals)
	If totals.Length < names.Length + 1
		Return
	EndIf
	Int i = 0
	While i < names.Length
		AddTextOption(names[i], totals[i] as String, OPTION_FLAG_DISABLED)
		i += 1
	EndWhile
	AddTextOption(AgeOfSkyrim.Tr("mcm.stock.unclassified"), totals[names.Length] as String, OPTION_FLAG_DISABLED)
EndFunction

Function PageRealm()
	String mine = AgeOfSkyrim.GetPlayerFaction()

	AddHeaderOption(AgeOfSkyrim.Tr("mcm.realm.economyHeader"))
	AddTextOption(AgeOfSkyrim.Tr("mcm.realm.economySoon"), "", OPTION_FLAG_DISABLED)
	AddEmptyOption()
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.realm.routesHeader"))
	AddTextOption(AgeOfSkyrim.Tr("mcm.realm.routesSoon"), "", OPTION_FLAG_DISABLED)
	SetCursorPosition(1)
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.realm.governmentHeader"))
	AddTextOption(AgeOfSkyrim.Tr("mcm.realm.governmentSoon"), "", OPTION_FLAG_DISABLED)

	; M27 (ADR 0057): marco permanente, só leitura.
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.realm.dominationHeader"))
	If mine != "" && AgeOfSkyrim.HasDominated(mine)
		AddTextOption(AgeOfSkyrim.Tr("mcm.realm.domination"), AgeOfSkyrim.Tr("mcm.yes"), OPTION_FLAG_DISABLED)
	Else
		AddTextOption(AgeOfSkyrim.Tr("mcm.realm.domination"), AgeOfSkyrim.Tr("mcm.no"), OPTION_FLAG_DISABLED)
	EndIf
EndFunction

Function PageDebug()
	Bool on = AgeOfSkyrim.IsDebugEnabled()
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.debug.header"))
	AddTextOption(AgeOfSkyrim.Tr("mcm.debug.version"), AgeOfSkyrim.Tr2("mcm.debug.versionValue", AgeOfSkyrim.GetPluginVersion(), AgeOfSkyrim.GetApiVersion() as String), OPTION_FLAG_DISABLED)
	If on
		AddTextOption(AgeOfSkyrim.Tr("mcm.debug.enabled"), AgeOfSkyrim.Tr("mcm.yes"), OPTION_FLAG_DISABLED)
	Else
		AddTextOption(AgeOfSkyrim.Tr("mcm.debug.enabled"), AgeOfSkyrim.Tr("mcm.no"), OPTION_FLAG_DISABLED)
	EndIf
	AddTextOption(AgeOfSkyrim.Tr("mcm.debug.territories"), AgeOfSkyrim.GetTerritoryCount() as String, OPTION_FLAG_DISABLED)
	AddTextOption(AgeOfSkyrim.Tr("mcm.debug.claims"), AgeOfSkyrim.Tr2("mcm.debug.claimsValue", AgeOfSkyrim.GetClaimCount() as String, AgeOfSkyrim.GetMaxClaims() as String), OPTION_FLAG_DISABLED)
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.debug.hotkeys"))
	AddTextOption(AgeOfSkyrim.Tr("mcm.debug.hotkeyOwner"), KeyLabel(AgeOfSkyrim.GetDebugHotkey("owner")), OPTION_FLAG_DISABLED)
	AddTextOption(AgeOfSkyrim.Tr("mcm.debug.hotkeySelfTest"), KeyLabel(AgeOfSkyrim.GetDebugHotkey("selftest")), OPTION_FLAG_DISABLED)
	AddTextOption(AgeOfSkyrim.Tr("mcm.debug.hotkeyStock"), KeyLabel(AgeOfSkyrim.GetDebugHotkey("stock")), OPTION_FLAG_DISABLED)
	AddTextOption(AgeOfSkyrim.Tr("mcm.debug.hotkeyClaim"), KeyLabel(AgeOfSkyrim.GetDebugHotkey("claim")), OPTION_FLAG_DISABLED)

	SetCursorPosition(1)
	AddHeaderOption(AgeOfSkyrim.Tr("mcm.debug.actions"))
	Int flags = OPTION_FLAG_DISABLED
	If on
		flags = OPTION_FLAG_NONE
	EndIf
	_oSelfTest = AddTextOption(AgeOfSkyrim.Tr("mcm.debug.selfTest"), AgeOfSkyrim.Tr("mcm.debug.run"), flags)
	_oStock = AddTextOption(AgeOfSkyrim.Tr("mcm.debug.stock"), AgeOfSkyrim.Tr("mcm.debug.run"), flags)
	_oClaim = AddTextOption(AgeOfSkyrim.Tr("mcm.debug.claim"), AgeOfSkyrim.Tr("mcm.debug.run"), flags)
EndFunction

; DIK -> rótulo curto (F1..F12; o resto mostra o código).
String Function KeyLabel(Int aiCode)
	If aiCode >= 59 && aiCode <= 68
		Return "F" + (aiCode - 58)
	ElseIf aiCode == 87
		Return "F11"
	ElseIf aiCode == 88
		Return "F12"
	EndIf
	Return aiCode as String
EndFunction

Int Function TerritoryIndexOf(Int a_option)
	Int i = 0
	While i < _terrKeys.Length
		If _terrOpts[i] == a_option
			Return i
		EndIf
		i += 1
	EndWhile
	Return -1
EndFunction

Int Function IndexOf(String[] list, String value)
	Int i = 0
	While i < list.Length
		If list[i] == value
			Return i
		EndIf
		i += 1
	EndWhile
	Return 0
EndFunction

; ---- Interação ----------------------------------------------------------------------------------
Event OnOptionSelect(Int a_option)
	If a_option == _oCreate
		If _newName == ""
			ShowMessage(AgeOfSkyrim.Tr("mcm.faction.needName"), False)
			Return
		EndIf
		String created = AgeOfSkyrim.CreatePlayerFaction(_newName, _newStyle, _newColor)
		If created == ""
			ShowMessage(AgeOfSkyrim.Tr("mcm.faction.createFailed"), False)
		Else
			ShowMessage(AgeOfSkyrim.Tr1("mcm.faction.created", AgeOfSkyrim.GetFactionName(created)), False)
		EndIf
		ForcePageReset()
	ElseIf a_option == _oPrev
		_terrPage -= 1
		ForcePageReset()
	ElseIf a_option == _oNext
		_terrPage += 1
		ForcePageReset()
	ElseIf a_option == _oSelfTest
		AgeOfSkyrim.RunSelfTest()
		ShowMessage(AgeOfSkyrim.Tr("mcm.debug.seeHud"), False)
	ElseIf a_option == _oStock
		AgeOfSkyrim.ShowPlayerStock()
		ShowMessage(AgeOfSkyrim.Tr("mcm.debug.seeHud"), False)
	ElseIf a_option == _oClaim
		AgeOfSkyrim.PlantClaim()
		ShowMessage(AgeOfSkyrim.Tr("mcm.debug.seeHud"), False)
		ForcePageReset()
	EndIf
EndEvent

Event OnOptionHighlight(Int a_option)
	Int t = TerritoryIndexOf(a_option)
	If t >= 0
		String tKey = _terrKeys[t]
		SetInfoText(AgeOfSkyrim.Tr2("mcm.terr.info", AgeOfSkyrim.Tr("mcm.type." + AgeOfSkyrim.GetTerritoryType(tKey)), AgeOfSkyrim.GetTerritoryProvince(tKey)))
	ElseIf a_option == _oCreate
		SetInfoText(AgeOfSkyrim.Tr("mcm.faction.createInfo"))
	ElseIf a_option == _oStyle
		SetInfoText(AgeOfSkyrim.Tr("mcm.faction.styleInfo"))
	EndIf
EndEvent

Event OnOptionInputOpen(Int a_option)
	If a_option == _oName
		String mine = AgeOfSkyrim.GetPlayerFaction()
		If mine == ""
			SetInputDialogStartText(_newName)
		Else
			SetInputDialogStartText(AgeOfSkyrim.GetFactionName(mine))
		EndIf
	EndIf
EndEvent

Event OnOptionInputAccept(Int a_option, String a_input)
	If a_option != _oName || a_input == ""
		Return
	EndIf
	String mine = AgeOfSkyrim.GetPlayerFaction()
	If mine == ""
		_newName = a_input
	Else
		AgeOfSkyrim.RenameFaction(mine, a_input)
	EndIf
	ForcePageReset()
EndEvent

Event OnOptionMenuOpen(Int a_option)
	If a_option == _oStyle
		SetMenuDialogOptions(AgeOfSkyrim.GetStyleNames())
		String current = _newStyle
		String mine = AgeOfSkyrim.GetPlayerFaction()
		If mine != ""
			current = AgeOfSkyrim.GetFactionStyle(mine)
		EndIf
		SetMenuDialogStartIndex(IndexOf(_styles, current))
		SetMenuDialogDefaultIndex(0)
		Return
	EndIf
	Int t = TerritoryIndexOf(a_option)
	If t >= 0
		_menuTerritory = t
		_ownerChoices = AgeOfSkyrim.GetOwnerChoices()
		SetMenuDialogOptions(AgeOfSkyrim.GetOwnerChoiceNames())
		SetMenuDialogStartIndex(IndexOf(_ownerChoices, AgeOfSkyrim.GetTerritoryOwner(_terrKeys[t])))
		SetMenuDialogDefaultIndex(0)
	EndIf
EndEvent

Event OnOptionMenuAccept(Int a_option, Int a_index)
	If a_index < 0
		Return
	EndIf
	If a_option == _oStyle
		If a_index >= _styles.Length
			Return
		EndIf
		String mine = AgeOfSkyrim.GetPlayerFaction()
		If mine == ""
			_newStyle = _styles[a_index]
		Else
			AgeOfSkyrim.SetFactionStyle(mine, _styles[a_index])
		EndIf
		SetMenuOptionValue(a_option, AgeOfSkyrim.GetStyleName(_styles[a_index]))
		Return
	EndIf
	Int t = TerritoryIndexOf(a_option)
	If t >= 0 && a_index < _ownerChoices.Length
		If AgeOfSkyrim.SetTerritoryOwner(_terrKeys[t], _ownerChoices[a_index])
			SetMenuOptionValue(a_option, AgeOfSkyrim.GetFactionName(_ownerChoices[a_index]))
		EndIf
	EndIf
EndEvent

Event OnOptionColorOpen(Int a_option)
	If a_option == _oColor
		String mine = AgeOfSkyrim.GetPlayerFaction()
		If mine == ""
			SetColorDialogStartColor(_newColor)
		Else
			SetColorDialogStartColor(AgeOfSkyrim.GetFactionColor(mine))
		EndIf
		SetColorDialogDefaultColor(0xC0C0C0)
	EndIf
EndEvent

Event OnOptionColorAccept(Int a_option, Int a_color)
	If a_option != _oColor
		Return
	EndIf
	String mine = AgeOfSkyrim.GetPlayerFaction()
	If mine == ""
		_newColor = a_color
	Else
		AgeOfSkyrim.SetFactionColor(mine, a_color)
	EndIf
	SetColorOptionValue(a_option, a_color)
EndEvent
