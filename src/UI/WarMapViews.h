#pragma once

// Interno do mapa tático (M11): as duas "telas" que o WarMap usa. Outros módulos usam só UI/WarMap.h.
namespace AoS::WarMapPrisma
{
    // Pede a API do PrismaUI. false se o PrismaUI não está instalado (ou a V1 não existe).
    bool Init();
    // Thread principal. Cria a view na primeira vez (lazy), mostra e foca. false se a view não pôde ser criada.
    bool Open();
    void Close();
    [[nodiscard]] bool IsOpen();
    // Envia o snapshot (window.aosWarMapData) e o resultado de uma ação (window.aosWarMapResult).
    // Antes do DOM pronto, o snapshot fica guardado e é enviado no onDomReady.
    void PushData(const std::string& a_json);
    void PushResult(const std::string& a_json);
}

namespace AoS::WarMapMenus
{
    // Thread principal. Menus vanilla (MessageBox) — fallback sem PrismaUI. false se o MessageBox não existe.
    bool Open();
}
