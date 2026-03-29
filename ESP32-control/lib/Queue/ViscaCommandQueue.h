#ifndef VISCACOMMANDQUEUE_H
#define VISCACOMMANDQUEUE_H

#include "MoveQueue.h"
#include "ViscaListener.h"

namespace movequeue {

using ViscaCommandQueue = MoveQueue<visca::ViscaCommandBase>;
}

#endif