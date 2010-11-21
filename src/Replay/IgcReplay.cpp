/*
  Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2010 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Replay/IgcReplay.hpp"
#include <algorithm>

#include "Navigation/GeoPoint.hpp"
#include "StringUtil.hpp"

IgcReplay::IgcReplay() :
  TimeScale(1.0),
  Enabled(false),
  cli(fixed(0.98)),
  reader(NULL)
{
  FileName[0] = _T('\0');
}

bool
IgcReplay::ScanBuffer(const TCHAR* buffer, fixed &Time,
                      fixed &Latitude, fixed &Longitude, fixed &Altitude,
                      fixed &PressureAltitude)
{
  int DegLat, DegLon;
  int MinLat, MinLon;
  TCHAR NoS, EoW;
  int iAltitude, iPressureAltitude;
  int Hour = 0;
  int Minute = 0;
  int Second = 0;
  int lfound =
      _stscanf(buffer, _T("B%02d%02d%02d%02d%05d%c%03d%05d%cA%05d%05d"),
      &Hour, &Minute, &Second, &DegLat, &MinLat, &NoS, &DegLon,
      &MinLon, &EoW, &iPressureAltitude, &iAltitude);

  if (lfound == EOF)
    return false;

  if (lfound != 11)
    return false;

  Latitude = fixed(DegLat) + fixed(MinLat) / 60000;
  if (NoS == _T('S'))
    Latitude *= -1;

  Longitude = fixed(DegLon) + fixed(MinLon) / 60000;
  if (EoW == _T('W'))
    Longitude *= -1;

  Altitude = fixed(iAltitude);
  PressureAltitude = fixed(iPressureAltitude);
  Time = fixed(Hour * 3600 + Minute * 60 + Second);
  return true;
}

bool
IgcReplay::ReadPoint(fixed &Time, fixed &Latitude, fixed &Longitude,
                     fixed &Altitude, fixed &PressureAltitude)
{
  TCHAR *buffer;

  while ((buffer = reader->read()) != NULL) {
    if (ScanBuffer(buffer, Time, Latitude, Longitude, Altitude, PressureAltitude))
      return true;
  }

  return false;
}

bool
IgcReplay::update_time(const fixed mintime)
{
  const fixed t_simulation_last = t_simulation;

  t_simulation += fixed_one * TimeScale;
  t_simulation = std::max(mintime, t_simulation);

  return (t_simulation > t_simulation_last);
}

void
IgcReplay::reset_time()
{
  t_simulation = fixed_zero;
}

void
IgcReplay::Stop()
{
  CloseFile();

  on_stop();

  Enabled = false;
}

void
IgcReplay::Start()
{
  if (Enabled)
    Stop();

  if (!OpenFile()) {
    on_bad_file();
    return;
  }

  cli.Reset();
  reset_time();
  on_reset();

  Enabled = true;
}

const TCHAR*
IgcReplay::GetFilename()
{
  return FileName;
}

void
IgcReplay::SetFilename(const TCHAR *name)
{
  if (!name || string_is_empty(name))
    return;

  if (_tcscmp(FileName, name) != 0)
    _tcscpy(FileName, name);
}

bool
IgcReplay::Update()
{
  if (!Enabled)
    return false;

  if (!update_time(cli.GetMinTime()))
    return true;

  // if need a new point
  while (cli.NeedData(t_simulation) && Enabled) {
    fixed t1 = fixed_zero;
    fixed Lat1, Lon1, Alt1, PAlt1;
    Enabled = ReadPoint(t1, Lat1, Lon1, Alt1, PAlt1);

    if (Enabled && positive(t1))
      cli.Update(t1, Lon1, Lat1, Alt1, PAlt1);
  }

  if (t_simulation == fixed_zero)
    t_simulation = cli.GetMaxTime();

  if (!Enabled) {
    Stop();
  } else {
    fixed Alt, PAlt;
    GeoPoint Pos;

    cli.Interpolate(t_simulation, Pos, Alt, PAlt);

    const fixed Speed = cli.GetSpeed(t_simulation);
    const Angle Bearing = cli.GetBearing(t_simulation);

    on_advance(Pos, Speed, Bearing, Alt, PAlt, t_simulation);
  }

  return Enabled;
}

bool
IgcReplay::OpenFile()
{
  if (reader)
    return true;

  if (string_is_empty(FileName))
    return false;

  reader = new FileLineReader(FileName);
  if (!reader->error())
    return true;

  return false;
}

void
IgcReplay::CloseFile()
{
  if (!reader)
    return;

  delete reader;
  reader = NULL;
}
