/*
===========================================================================

Jill of the Jungle Reconstructed
Copyright (C) 2026 Justin Marshall(IceColdDuke).

This file is part of the Jill of the Jungle Reconstructed Source Code ("Jill of the Jungle Reconstructed").

Jill of the Jungle Reconstructed is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Jill of the Jungle Reconstructed is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Jill of the Jungle Reconstructed.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

/*
 * Android port note (this file, not upstream): handler() is the recovered DOS keyboard-
 * interrupt body, already reduced to an empty stub upstream itself (real input arrives via
 * HOSTSDL.C/HOSTANDROID.c's host_* layer instead -- see jill/KEYBOARD.c's own header
 * comment) -- nothing left to port beyond the filename. Renamed KEYINTR.C -> KEYINTR.c for
 * the same CMake gotcha as jill/GR.c (see that file's own header comment).
 */

#include "KEYBOARD.H"

void handler(void)
{
}
