#pragma once

// [Debug] bTelemetry only: commands for the UAT harness that Skyrim's console can't do, read from
// Data/SKSE/Plugins/TES64_debug.txt (one per line, the file is deleted once run):
//   equipshout <formid hex>
//   cast <spell formid hex>     (on the player, by the player)
namespace DebugChannel
{
	void Poll();
}
