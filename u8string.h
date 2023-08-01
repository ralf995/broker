//
// u8string.h
//

#ifndef _u8string_h
#define _u8string_h

#include <circle/types.h>

class U8String {
public:
  U8String(void);
  U8String(const unsigned char string[], const unsigned int length);
  U8String(U8String const &u8string);
  U8String(U8String const &u8string, const unsigned int length, const unsigned int start = 0);
  ~U8String(void);

  void operator=(U8String const &u8string);
  boolean operator==(U8String const &other);

  void Print(const char *name, const char *header = "");

  unsigned int GetLength(void) const;
  unsigned char *GetString(void) const;

  void SetString(const unsigned char *string);
  void SetLength(const unsigned int length);

private:
  unsigned char *m_pString;
  unsigned int  m_Length;
};

#endif // !_u8string_h
