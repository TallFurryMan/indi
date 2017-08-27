/*
    INDI 3rd party driver
    Lacerta MGen Autoguider INDI driver, implemented with help from
    Tommy (teleskopaustria@gmail.com) and Zoltan (mgen@freemail.hu).

    Teleskop & Mikroskop Zentrum (www.teleskop.austria.com)
    A-1050 WIEN, Schönbrunner Strasse 96
    +43 699 1197 0808 (Shop in Wien und Rechnungsanschrift)
    A-4020 LINZ, Gärtnerstrasse 16
    +43 699 1901 2165 (Shop in Linz)

    Lacerta GmbH
    UmsatzSt. Id. Nr.: AT U67203126
    Firmenbuch Nr.: FN 379484s

    Copyright (C) 2017 by TallFurryMan (eric.dejouhanet@gmail.com)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

/*
 * mgcmd_read_guide_frame.h
 *
 *  Created on: 23 août 2017
 *      Author: TallFurryMan
 */

#ifndef _3RDPARTY_INDI_MGEN_MGCMD_READ_GUIDE_FRAME_H_
#define _3RDPARTY_INDI_MGEN_MGCMD_READ_GUIDE_FRAME_H_

#include "mgc.h"

class MGCMD_READ_GUIDE_FRAME : MGC
{
  public:
    virtual IOByte opCode() const { return 0x9D; }
    virtual IOMode opMode() const { return OPM_APPLICATION; }

  public:
    float ascension_drift()   const { return (float) (answer[4] + answer[3] * 256) / 256.0f - (answer[3] > 127 ? 256.0f : 0.0f); }
    float declination_drift() const { return (float) (answer[6] + answer[5] * 256) / 256.0f - (answer[5] > 127 ? 256.0f : 0.0f); }

  public:
    char frame_index()    const { return answer[2] % 64; }
    bool has_guide_star() const { return (answer[2] & 0x40) == 0x40; }

  public:
    virtual IOResult ask(MGenDevice &root) //throw(IOError)
    {
        if (CR_SUCCESS != MGC::ask(root))
            return CR_FAILURE;

        if (root.lock())
        {
            root.write(query);

            int const bytes_read = root.read(answer);

            root.unlock();

            /* There is one additional byte which is not documented:
             * 0x9D
             * 0x01
             * Frame number + star present on b6 (frame number all ones in no star?)
             * RA_FRAC RA_INT
             * DEC_FRAC DEC_INT
             */
            if (answer[0] == query[0] && (1 + 1 + 2 + 2 + 1 == bytes_read))
                return CR_SUCCESS;

            _E("no ack (%d bytes read)", bytes_read);
        }

        return CR_FAILURE;
    }

  public:
    MGCMD_READ_GUIDE_FRAME() : MGC(IOBuffer{ opCode(), 1 }, IOBuffer(1 + 1 + 2 + 2 + 1)){};
};

#endif /* _3RDPARTY_INDI_MGEN_MGCMD_READ_GUIDE_FRAME_H_ */
