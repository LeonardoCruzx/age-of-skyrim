Scriptname AgeOfSkyrim Native Hidden
{Age of Skyrim: funções nativas (AgeOfSkyrim.dll) para o MCM e para outros mods só-script.

 Facções e territórios são identificados pela CHAVE de texto ("imperial", "player_1",
 "Skyrim.esm|0x01A2B3"); "" = neutro / nenhum. Sem o DLL carregado, toda chamada falha
 (GetApiVersion() devolve 0) — confira antes de usar.

 Eventos (RegisterForModEvent). Assinatura: Event Nome(String asEventName, String asKey, Float afValue, Form akSender)
   AoS_StateReady        — estado pronto (novo jogo / save carregado)
   AoS_TerritoryChanged  — asKey = território (GetTerritoryOwner / GetTerritoryPreviousOwner)
   AoS_FactionCreated    — asKey = facção
   AoS_FactionChanged    — asKey = facção (nome, estilo ou cor)
   AoS_ClaimCreated      — afValue = nº de claims, akSender = estandarte
   AoS_StockChanged      — akSender = container recontado
   AoS_SeatFounded       — (a partir da M6) asKey = facção
   AoS_SeatMoved         — (a partir da M6) asKey = facção
   AoS_TreasuryChanged   — (a partir da M12) asKey = facção, afValue = saldo
   AoS_TradeRouteChanged — (a partir da M13) asKey = facção
   AoS_GovernmentChanged — (a partir da M18) asKey = facção
   AoS_PopulationChanged — (a partir da M19) asKey = território, afValue = população
   AoS_TitleGranted      — (a partir da M22) asKey = facção, afValue = título}

; ---- Geral -------------------------------------------------------------------------------------
Int Function GetApiVersion() Global Native             ; 0 = DLL ausente
String Function GetPluginVersion() Global Native
String Function Tr(String asKey) Global Native         ; texto traduzido (idioma do settings.json)
String Function Tr1(String asKey, String asArg1) Global Native
String Function Tr2(String asKey, String asArg1, String asArg2) Global Native
Bool Function IsDebugEnabled() Global Native
Int Function GetDebugHotkey(String asWhich) Global Native  ; "owner", "selftest", "stock", "claim"

; ---- Facções -----------------------------------------------------------------------------------
String[] Function GetFactions() Global Native
String Function GetFactionName(String asFaction) Global Native
String Function GetFactionStyle(String asFaction) Global Native
Int Function GetFactionColor(String asFaction) Global Native   ; 0xRRGGBB, -1 = não existe
Bool Function IsPlayerFaction(String asFaction) Global Native
String Function GetPlayerFaction() Global Native               ; "" = jogador ainda não criou
String Function CreatePlayerFaction(String asName, String asStyle, Int aiColor) Global Native
Bool Function RenameFaction(String asFaction, String asName) Global Native
Bool Function SetFactionStyle(String asFaction, String asStyle) Global Native
Bool Function SetFactionColor(String asFaction, Int aiColor) Global Native
Int Function CountTerritoriesOwnedBy(String asFaction) Global Native
String[] Function GetStyles() Global Native
String Function GetStyleName(String asStyle) Global Native
String[] Function GetStyleNames() Global Native                 ; paralelo a GetStyles()
String[] Function GetOwnerChoices() Global Native               ; "" (neutro) + todas as facções
String[] Function GetOwnerChoiceNames() Global Native           ; paralelo a GetOwnerChoices()

; ---- Territórios -------------------------------------------------------------------------------
Int Function GetTerritoryCount() Global Native
String[] Function GetTerritories(Int aiOffset, Int aiCount) Global Native  ; ordenados por nome, máx. 128
String Function GetTerritoryName(String asTerritory) Global Native
String Function GetTerritoryOwner(String asTerritory) Global Native
String Function GetTerritoryPreviousOwner(String asTerritory) Global Native
String Function GetTerritoryType(String asTerritory) Global Native     ; fort, tower, camp, village, city, unknown
String Function GetTerritoryProvince(String asTerritory) Global Native
String Function GetTerritoryForLocation(Location akLocation) Global Native
Bool Function SetTerritoryOwner(String asTerritory, String asFaction) Global Native

; ---- Estoque (último elemento = itens sem categoria) -------------------------------------------
String[] Function GetStockCategories() Global Native
String[] Function GetStockCategoryNames() Global Native
Int[] Function GetContainerStockTotals(ObjectReference akContainer) Global Native
Int[] Function GetPlayerStockTotals() Global Native

; ---- Sede (M6), tesouro (M12), rotas (M13), governo (M18): "não disponível" até lá -------------
Bool Function HasSeat(String asFaction) Global Native
String Function GetSeatTerritory(String asFaction) Global Native
ObjectReference Function GetSeatChest(String asFaction) Global Native
Int Function GetTreasury(String asFaction) Global Native
Int Function GetTradeRouteCount(String asFaction) Global Native
Int Function GetGovernmentId(String asFaction) Global Native

; ---- Free Claim e debug ------------------------------------------------------------------------
Int Function GetClaimCount() Global Native
Int Function GetMaxClaims() Global Native
Function RunSelfTest() Global Native
Function ShowPlayerStock() Global Native
Function PlantClaim() Global Native
