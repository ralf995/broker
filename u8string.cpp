//
// u8string.cpp
//

#include <circle/logger.h>
#include <circle/util.h>
#include "u8string.h"

U8String::U8String(void) : m_pString(0), m_Length(0) { }

U8String::U8String(const unsigned char string[], const unsigned int length) {
  m_Length = length;
  m_pString = new unsigned char[m_Length];
  memcpy(m_pString, string, m_Length);
}

U8String::U8String(U8String const &u8string) {
  m_Length = u8string.m_Length;
  m_pString = new unsigned char[m_Length];
  memcpy(m_pString, u8string.m_pString, m_Length);
}

U8String::U8String(U8String const &u8string, const unsigned int length, const unsigned int start) {
  m_Length = length;
  m_pString = new unsigned char[m_Length];
  memcpy(m_pString, &(u8string.GetString())[start], m_Length);
}

U8String::~U8String(void) {
  delete [] m_pString;
  m_pString = 0;
}

void U8String::operator=(U8String const &other) {
  m_Length = other.m_Length;
  m_pString = new unsigned char[m_Length];
  memcpy(m_pString, other.m_pString, m_Length);
}

boolean U8String::operator==(U8String const &other) {
  if (m_Length != other.GetLength()) return FALSE;
  for (unsigned int i = 0; i < m_Length; i++) {
    if (m_pString[i] != other.GetString()[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

void U8String::Print(const char *name, const char *header) {
  if (m_Length == 0) return;

  if (strcmp(header, "") != 0) {
    CLogger::Get()->Write(name, LogNotice, "%s", header);
  }
  u8 string[m_Length+1];
  memcpy(string, m_pString, m_Length);
  string[m_Length] = '\0';
  CLogger::Get()->Write(name, LogNotice, "%s", string);
}

unsigned char *U8String::GetString(void) const {
  return m_pString;
}

unsigned int U8String::GetLength(void) const {
  return m_Length;
}

void U8String::SetString(const unsigned char *string) {
  memcpy(m_pString, string, m_Length);
}

void U8String::SetLength(const unsigned int length) {
  m_Length = length;
}

