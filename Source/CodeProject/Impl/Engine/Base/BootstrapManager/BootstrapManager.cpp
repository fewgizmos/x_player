#include "X/Engine/Base/BootstrapManager/BootstrapManager.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include <exception>

namespace Engine::Base::BootstrapManager::It
{

void InitAll()
{
    for (const Entry& Item : DeferredBootstrapRegistration::GetInitEntries()) {
        Item.Func();
    }
}

void CleanAll()
{
    std::exception_ptr Failure;

    for (const Entry& Item : DeferredBootstrapRegistration::GetCleanEntries()) {
        try {
            Item.Func();
        }
        catch (...) {
            if (!Failure) {
                Failure = std::current_exception();
            }
        }
    }

    if (Failure) {
        std::rethrow_exception(Failure);
    }
}

}
