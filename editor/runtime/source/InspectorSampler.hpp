#pragma once
#include "InspectorProtocol.hpp"
namespace YYTK { struct YYTKInterface; }
namespace RunnerEditor {
// Only call on established game thread. Returns false on SEH/unsafe runner fault;
// normal unavailable/destroyed/budget errors return true with an error page.
bool SampleInspector(YYTK::YYTKInterface* yytk, const InspectRequest& request, InspectResponse& response);
}
