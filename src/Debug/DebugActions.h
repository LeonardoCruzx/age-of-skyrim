#pragma once

// M30: cada ação de debug (antes só inline em Events.cpp, uma por hotkey) vira uma função com nome aqui,
// chamada tanto pela hotkey original (Events.cpp) quanto pelo menu de debug (UI/DebugMenu.cpp) — uma só
// implementação, nunca desalinha entre tecla e item de menu.
namespace AoS::DebugActions
{
    void CycleOwner();        // F10 / "-" no menu: alterna o dono de debug.territory
    void SelfTest();          // F11: autoteste (bandeiras, facções, persistência)
    void ReportStock();       // F8: estoque do jogador (e da sede, se houver)
    void TryClaimHere();      // F3: Free Claim, finca o estandarte
    void SimulateConquest();  // F4: simula a conquista do local atual (M5)
    void EconomyHours();      // F6: avança debug.economyHours horas de economia
    void CreateRoute();       // F7: rota entre os 2 territórios seus mais próximos
    void OpenWarMap();        // M11: abre a mesa de guerra de qualquer lugar
    void GarrisonHere();      // M14: guarnição do território atual + spawn forçado
    void UpgradeHere();       // M15: inicia a próxima melhoria disponível
    void AiTick();            // M16: força o tick estratégico da IA
    void DiplomacyCycle();    // M17: alterna a relação com o dono do território atual
    void OfferGovernor();     // M24: oferece um governador na capital da província atual
    void LawCapture();        // M25: simula uma captura de prisioneiros
    void LawRansom();         // M25: resgata o 1º prisioneiro da facção do jogador
    void LawExecute();        // M25: executa o 1º prisioneiro da facção do jogador
    void StabilityHere();     // M26: estabilidade do território atual + checagem de rebelião
    void RealmSummary();      // M18/M19/M22: resumo do reino na tela
    void RealmHours();        // M18/M19/M22: simula debug.realmDebugHours horas do reino
    void RealmDoctrine();     // M18: troca para a próxima doutrina
    void RealmConquerNearby();  // M18: dá à facção do jogador os territórios mais próximos
    void ArmyRecruit();       // M20: recruta soldados de campo no território atual
    void ArmyList();          // M20: lista os exércitos da facção do jogador
    void BattleTest();        // M21: força uma batalha de teste perto do jogador
}
