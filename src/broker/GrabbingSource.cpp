#include "broker/GrabbingSource.h"

namespace contextdeck::broker {

GrabbingSource::GrabbingSource(SourceTag tag, IGrabber &grabber)
    : tag_(tag)
    , grabber_(grabber)
{
}

bool GrabbingSource::openSource()
{
    if (!openOk) {
        opened_ = false;
        return false;
    }
    opened_ = true;
    return true;
}

bool GrabbingSource::claimSource()
{
    return grabber_.grab();
}

void GrabbingSource::unclaimSource()
{
    grabber_.ungrab();
}

void GrabbingSource::closeSource()
{
    opened_ = false;
}

} // namespace contextdeck::broker
