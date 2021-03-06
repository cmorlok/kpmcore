/*************************************************************************
 *  Copyright (C) 2008 by Volker Lanz <vl@fidra.de>                      *
 *                                                                       *
 *  This program is free software; you can redistribute it and/or        *
 *  modify it under the terms of the GNU General Public License as       *
 *  published by the Free Software Foundation; either version 3 of       *
 *  the License, or (at your option) any later version.                  *
 *                                                                       *
 *  This program is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
 *  GNU General Public License for more details.                         *
 *                                                                       *
 *  You should have received a copy of the GNU General Public License    *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 *************************************************************************/

#if !defined(COPYTARGETFILE__H)

#define COPYTARGETFILE__H

#include "core/copytarget.h"

#include <QtGlobal>
#include <QFile>

class QString;

/** A file to copy to.

    Repesents a target file to copy to. Used to back up a FileSystem to a file.

    @see CopySourceFile, CopyTargetDevice
    @author Volker Lanz <vl@fidra.de>
*/
class CopyTargetFile : public CopyTarget
{
public:
    CopyTargetFile(const QString& filename, qint32 sectorsize);

public:
    bool open() override;
    bool writeSectors(void* buffer, qint64 writeOffset, qint64 numSectors) override;

    qint32 sectorSize() const override {
        return m_SectorSize;    /**< @return the file's sector size */
    }
    qint64 firstSector() const override {
        return 0;    /**< @return always 0 for a file */
    }
    qint64 lastSector() const override {
        return sectorsWritten();    /**< @return the number of sectors written so far */
    }

protected:
    QFile& file() {
        return m_File;
    }
    const QFile& file() const {
        return m_File;
    }

protected:
    QFile m_File;
    qint32 m_SectorSize;
};

#endif
