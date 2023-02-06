/*
* author: kanghyki
* email: kanghyki@gmail.com
* created: 2023-01-23 21:03:19
* updated: 2023-02-06 17:37:17
*/

#ifndef LEXER_HPP
#define LEXER_HPP

#include "token.hpp"

#include <string>

class Lexer {
  public:
    Lexer(std::string input);
    ~Lexer();

    Token *NextToken();

  private:
    std::string input;
    int position;
    int read_position;
    char ch;

    void ReadChar();
    char PeekChar();
    std::string ReadIdentifier();
    std::string ReadNumber();
    bool IsDigit(char ch);
    bool IsLetter(char ch);
    bool IsSpace(char ch);
    std::string LookupIdent(std::string ident);
    void SkipWhitespace();

    Lexer();
    Lexer(Lexer const &obj);
    Lexer& operator=(Lexer const &obj);
};

#endif
