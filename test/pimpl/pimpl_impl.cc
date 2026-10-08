#include "pimpl_target.h"

#include <memory>

struct PimplTarget::Impl {
    int value{0};
};

PimplTarget::PimplTarget() : impl_(std::make_unique<Impl>()) {}

int  PimplTarget::value() const { return impl_->value; }
void PimplTarget::setValue(int v) { impl_->value = v; }

PimplTarget& pimplTarget() {
    static PimplTarget instance;
    return instance;
}
