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
#ifndef _DRWFILE_H_
#define _DRWFILE_H_

#define DRW_VERSION 1070

struct DRW_Command
{
    union
    {
        struct  
        {
            unsigned int type0 : 2;
            unsigned int alpha : 8;
            unsigned int x : 11;
            unsigned int y : 11;
        };
    
        struct  
        {
            unsigned int type1 : 2;
            unsigned int col : 24;
            unsigned int flipx : 1;
            unsigned int flipy : 1;
        };

        struct  
        {
            unsigned int type2 : 2;
            unsigned int size : 16;
            unsigned int brushcontrol : 2;
            unsigned int brushtype : 2;
            unsigned int opacity : 8;
        };
    
        struct
        {
            unsigned int type : 2;
            unsigned int undef : 30;
        };
    
        int raw;
    };
};

struct DRW_Header
{
    static const unsigned int ID = 0x436f6c21; // 'Col!'

    unsigned int id;
    unsigned int version;
    int colorsversion_initial;
    int colorsversion_saved;
    int strokes;
    int time;
    int timessaved;
    int dummy[8];
    int ncommands;
};

#endif
