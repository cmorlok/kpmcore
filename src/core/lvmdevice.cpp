/*************************************************************************
 *  Copyright (C) 2016 by Chantara Tith <tith.chantara@gmail.com>        *
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

#include "core/lvmdevice.h"
#include "fs/filesystem.h"
#include "fs/filesystemfactory.h"
#include "core/partition.h"

#include "core/partitiontable.h"
#include "util/externalcommand.h"
#include "util/helpers.h"

#include <QRegularExpression>
#include <QStringList>
#include <KMountPoint>
#include <KDiskFreeSpaceInfo>
#include <KLocalizedString>

/** Constructs a representation of LVM device with functionning LV as Partition
 *
 *  @param name Volume Group name
 */
LvmDevice::LvmDevice(const QString& name, const QString& iconname)
    : VolumeManagerDevice(name,
                          (QStringLiteral("/dev/") + name),
                          getPeSize(name),
                          getTotalPE(name),
                          iconname,
                          Device::LVM_Device)
    , m_peSize(getPeSize(name))
    , m_totalPE(getTotalPE(name))
    , m_allocPE(getAllocatedPE(name))
    , m_freePE(getFreePE(name))
    , m_UUID(getUUID(name))
{
    initPartitions();
}

void LvmDevice::initPartitions()
{
    qint64 firstUsable = 0;
    qint64 lastusable  = totalPE() - 1;
    PartitionTable* pTable = new PartitionTable(PartitionTable::vmd, firstUsable, lastusable);

    foreach (Partition* p, scanPartitions(*this, pTable)) {
        pTable->append(p);
    }

    pTable->updateUnallocated(*this);

    setPartitionTable(pTable);
}

/**
 *  @returns sorted Partition(LV) Array
 */
QList<Partition*> LvmDevice::scanPartitions(const LvmDevice& dev, PartitionTable* pTable) const
{
    QList<Partition*> pList;
    foreach (QString lvPath, lvPathList()) {
        pList.append(scanPartition(lvPath, dev, pTable));
    }
    return pList;
}

/**
 * @returns sorted Partition(LV) Array
 */
Partition* LvmDevice::scanPartition(const QString& lvpath, const LvmDevice& dev, PartitionTable* pTable) const
{
    /*
     * NOTE:
     * LVM partition have 2 different start and end sector value
     * 1. representing the actual LV start from 0 -> size of LV - 1
     * 2. representing abstract LV's sector inside a VG partitionTable
     *    start from size of last Partitions -> size of LV - 1
     * Reason for this is for the LV Partition to worrks nicely with other parts of the codebase
     * without too many special cases.
     */

    qint64 startSector;
    qint64 endSector;
    qint64 lvSize;

    bool mounted = isMounted(lvpath);
    QString mountPoint = QString();

    KMountPoint::List mountPointList = KMountPoint::currentMountPoints(KMountPoint::NeedRealDeviceName);
    mountPointList.append(KMountPoint::possibleMountPoints(KMountPoint::NeedRealDeviceName));
    mountPoint = mountPointList.findByDevice(lvpath) ?
                 mountPointList.findByDevice(lvpath)->mountPoint() :
                 QString();

    lvSize      = getTotalLE(lvpath);
    startSector = mappedSector(lvpath,0);
    endSector   = startSector + (lvSize - 1);

    const KDiskFreeSpaceInfo freeSpaceInfo = KDiskFreeSpaceInfo::freeSpaceInfo(mountPoint);

    FileSystem* fs = FileSystemFactory::create(FileSystem::detectFileSystem(lvpath), 0, lvSize - 1);
    if (mounted && freeSpaceInfo.isValid() && mountPoint != QString()) {
        //TODO: fix used space report. currently incorrect
        fs->setSectorsUsed(freeSpaceInfo.used() / logicalSize());
    }

    if (fs->supportGetLabel() != FileSystem::cmdSupportNone) {
        fs->setLabel(fs->readLabel(lvpath));
    }

    Partition* part = new Partition(pTable,
                    dev,
                    PartitionRole(PartitionRole::Lvm_Lv),
                    fs,
                    startSector,
                    endSector,
                    lvpath,
                    PartitionTable::Flag::FlagLvm,
                    mountPoint,
                    mounted);
    return part;
}

qint64 LvmDevice::mappedSector(const QString& lvpath, qint64 sector) const
{
    qint64 mSector = 0;
    QList<QString> lvpathList = lvPathList();
    qint32 devIndex = lvpathList.indexOf(lvpath);

    if (devIndex) {
        for (int i = 0; i < devIndex; i++) {
            //TODO: currently going over the same LV again and again is wasteful. Could use some more optimization
            mSector += getTotalLE(lvpathList[i]);
        }
        mSector += sector;
    }
    return mSector;
}

QList<QString> LvmDevice::deviceNodeList() const
{
    QList<QString> devPathList;
    QString cmdOutput = getField(QStringLiteral("pv_name"), name());

    if (cmdOutput.size()) {
        QList<QString> tempPathList = cmdOutput.split(QStringLiteral("\n"), QString::SkipEmptyParts);
        foreach(QString devPath, tempPathList) {
            devPathList.append(devPath.trimmed());
        }
    }
    return devPathList;
}

QList<QString> LvmDevice::lvPathList() const
{
    QList<QString> lvPathList;
    QString cmdOutput = getField(QStringLiteral("lv_path"), name());

    if (cmdOutput.size()) {
        QList<QString> tempPathList = cmdOutput.split(QStringLiteral("\n"), QString::SkipEmptyParts);
        foreach(QString lvPath, tempPathList) {
            lvPathList.append(lvPath.trimmed());
        }
    }
    return lvPathList;
}

qint32 LvmDevice::getPeSize(const QString& vgname)
{
    QString val = getField(QStringLiteral("vg_extent_size"), vgname);
    return val.isEmpty() ? -1 : val.toInt();
}

qint32 LvmDevice::getTotalPE(const QString& vgname)
{
    QString val = getField(QStringLiteral("vg_extent_count"), vgname);
    return val.isEmpty() ? -1 : val.toInt();
}

qint32 LvmDevice::getAllocatedPE(const QString& vgname)
{
    return getTotalPE(vgname) - getFreePE(vgname);
}

qint32 LvmDevice::getFreePE(const QString& vgname)
{
    QString val =  getField(QStringLiteral("vg_free_count"), vgname);
    return val.isEmpty() ? -1 : val.toInt();
}

QString LvmDevice::getUUID(const QString& vgname)
{
    QString val = getField(QStringLiteral("vg_uuid"), vgname);
    return val.isEmpty() ? QStringLiteral("---") : val;

}

/** Get LVM vgs command output with field name
 *
 * @param fieldName lvm field name
 * @param vgname
 * @returns raw output of command output, usully with manay spaces within the returned string
 * */

QString LvmDevice::getField(const QString& fieldName, const QString& vgname)
{
    ExternalCommand cmd(QStringLiteral("lvm"),
            { QStringLiteral("vgs"),
              QStringLiteral("--foreign"),
              QStringLiteral("--readonly"),
              QStringLiteral("--noheadings"),
              QStringLiteral("--units"),
              QStringLiteral("B"),
              QStringLiteral("--nosuffix"),
              QStringLiteral("--options"),
              fieldName,
              vgname });
    if (cmd.run(-1) && cmd.exitCode() == 0) {
        return cmd.output().trimmed();
    }
    return QString();
}

qint32 LvmDevice::getTotalLE(const QString& lvpath)
{
    ExternalCommand cmd(QStringLiteral("lvm"),
            { QStringLiteral("lvdisplay"),
              lvpath});

    if (cmd.run(-1) && cmd.exitCode() == 0) {
        QRegularExpression re(QStringLiteral("Current LE\\h+(\\d+)"));
        QRegularExpressionMatch match = re.match(cmd.output());
        if (match.hasMatch()) {
             return  match.captured(1).toInt();
        }
    }
    return -1;
}

bool LvmDevice::removeLV(Report& report, LvmDevice& dev, Partition& part)
{
    ExternalCommand cmd(report, QStringLiteral("lvm"),
            { QStringLiteral("lvremove"),
              QStringLiteral("--yes"),
              part.partitionPath()});

    if (cmd.run(-1) && cmd.exitCode() == 0) {
        //TODO: remove Partition from PartitionTable and delete from memory ??
        dev.partitionTable()->remove(&part);
        return  true;
    }
    return false;
}

bool LvmDevice::createLV(Report& report, LvmDevice& dev, Partition& part, const QString& lvname)
{
    ExternalCommand cmd(report, QStringLiteral("lvm"),
            { QStringLiteral("lvcreate"),
              QStringLiteral("--yes"),
              QStringLiteral("--extents"),
              QString::number(part.length()),
              QStringLiteral("--name"),
              lvname,
              dev.name()});

    return (cmd.run(-1) && cmd.exitCode() == 0);
}

bool LvmDevice::resizeLv(Report& report, LvmDevice& dev, Partition& part)
{
    Q_UNUSED(dev);
    //TODO: through tests
    ExternalCommand cmd(report, QStringLiteral("lvm"),
            { QStringLiteral("lvresize"),
              //QStringLiteral("--yes"), // this command could corrupt user data
              QStringLiteral("--extents"),
              QString::number(part.length()),
              part.partitionPath()});

    return (cmd.run(-1) && cmd.exitCode() == 0);
}

bool LvmDevice::removePV(Report& report, LvmDevice& dev, const QString& pvPath)
{
    //TODO: through tests
    ExternalCommand cmd(report, QStringLiteral("lvm"),
            { QStringLiteral("vgreduce"),
              //QStringLiteral("--yes"), // potentially corrupt user data
              dev.name(),
              pvPath});

    return (cmd.run(-1) && cmd.exitCode() == 0);
}

bool LvmDevice::insertPV(Report& report, LvmDevice& dev, const QString& pvPath)
{
    //TODO: through tests
    ExternalCommand cmd(report, QStringLiteral("lvm"),
            { QStringLiteral("vgextend"),
              //QStringLiteral("--yes"), // potentially corrupt user data
              dev.name(),
              pvPath});

    return (cmd.run(-1) && cmd.exitCode() == 0);
}