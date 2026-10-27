#pragma once

namespace AoS
{
    // Mede um escopo e loga a duração ao sair: { ScopedTimer t{ "ApplyAll" }; ... }
    class ScopedTimer
    {
    public:
        explicit ScopedTimer(std::string_view a_what) noexcept :
            what_(a_what), start_(std::chrono::steady_clock::now())
        {}

        ~ScopedTimer()
        {
            const std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - start_;
            logger::info("[tempo] {}: {:.3f} ms", what_, ms.count());
        }

        ScopedTimer(const ScopedTimer&)            = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        std::string_view                      what_;
        std::chrono::steady_clock::time_point start_;
    };
}
