//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <circle/memory.h>
#include <circle/string.h>
#include "kernel.h"
#include "topictree.h"
#include "broker.h"

// Network configuration
// #define USE_DHCP

#define GPIO_OUTPUT_PIN 18

#ifndef USE_DHCP
static const u8 IPAddress[]      = {192, 168, 0, 111};
static const u8 NetMask[]        = {255, 255, 255, 0};
static const u8 DefaultGateway[] = {192, 168, 0, 1};
static const u8 DNSServer[]      = {192, 168, 0, 1};
#endif

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:  m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
  m_Timer (&m_Interrupt),
  m_Logger (m_Options.GetLogLevel (), &m_Timer),
  m_USBHCI (&m_Interrupt, &m_Timer),
  m_OutputPin(GPIO_OUTPUT_PIN, GPIOModeOutput)
#ifndef USE_DHCP
  , m_Net (IPAddress, NetMask, DefaultGateway, DNSServer)
#endif
{
  m_ActLED.Blink (5);  // show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
  boolean bOK = TRUE;

  if (bOK)
  {
    bOK = m_Screen.Initialize ();
  }

  if (bOK)
  {
    bOK = m_Serial.Initialize (115200);
  }

  if (bOK)
  {
    CDevice *pTarget = m_DeviceNameService.GetDevice(
        m_Options.GetLogDevice (),
        FALSE);
    if (pTarget == 0)
    {
      pTarget = &m_Screen;
    }

    bOK = m_Logger.Initialize (pTarget);
  }

  if (bOK)
  {
    bOK = m_Interrupt.Initialize ();
  }

  if (bOK)
  {
    bOK = m_Timer.Initialize ();
  }

  if (bOK)
  {
    bOK = m_USBHCI.Initialize ();
  }

  if (bOK)
  {
    bOK = m_Net.Initialize ();
  }

  return bOK;
}

TShutdownMode CKernel::Run (void)
{
  m_OutputPin.Write(LOW);
  m_Logger.Write(FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

  CString IPString;
  m_Net.GetConfig()->GetIPAddress()->Format (&IPString);
  m_Logger.Write(FromKernel,
                 LogNotice,
                 "The broker is running on address %s and port %u.",
                 (const char *) IPString, MQTT_PORT);

  CMutex mutex;
  TopicTree topicTree(&mutex);
  new Broker(&mutex, &topicTree, &m_Net);

  m_OutputPin.Write(HIGH);

  CMemorySystem memory = new CMemorySystem();

  int i = 0;
  for (unsigned nCount = 0; 1; nCount++)
  {
    m_Scheduler.Yield();
    m_Screen.Rotor(0, nCount);

    if (i == 100000) {
      i = 0;
      size_t remainingMemory = memory.GetHeapFreeSpace(HEAP_ANY);
      size_t totalMemory = memory.GetMemSize();
      size_t usedMemory = totalMemory - remainingMemory;
      m_Logger.Write(FromKernel, LogNotice, "Tot: %d; Free: %d, Used: %d", totalMemory, remainingMemory, usedMemory);
      // m_Logger.Write(FromKernel, LogNotice, "Free heap size: %d", remainingMemory);
      // m_Logger.Write(FromKernel, LogNotice, "Used memory: %d", usedMemory);
    } else i++;
  }

  return ShutdownHalt;
}