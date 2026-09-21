#include "charutil.h"

int is_lower(char c) { return (c >= 'a' && c <= 'z'); }
int is_upper(char c) { return (c >= 'A' && c <= 'Z'); }
char to_lower(char c) {
  if (is_upper(c)) {
    return c + ('a' - 'A');
  }
  return c;
}
char to_upper(char c) {
  if (is_lower(c)) {
    return c + ('A' - 'a');
  }
  return c;
}