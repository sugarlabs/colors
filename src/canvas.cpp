/*
    Copyright 2008 by Jens Andersson and Wade Brainerd.  
    This file is part of Colors! XO.

    Colors is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Colors is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Colors.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "canvas.h"

unsigned char BrushType::distance_tbl[DIST_TABLE_WIDTH][DIST_TABLE_WIDTH];

BrushType Brush::brush_type[BrushType::NUM_BRUSHES];

void test_method(void* data)
{
}


