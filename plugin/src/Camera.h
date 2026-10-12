#pragma once

// Skyrim's third-person camera retuned for Mario: centred on him (no over-the-shoulder offset) and lowered to his
// height. The game's own values come back when Mario mode turns off.
namespace Camera
{
	void Apply(float a_marioHeight);
	void Restore();
	bool EnsureThirdPerson();   // no first-person Mario: true if it had to switch
}
