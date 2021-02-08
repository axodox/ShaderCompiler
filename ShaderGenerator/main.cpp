#include "pch.h"
#include "exprtk.hpp"

using namespace winrt;
using namespace Windows::Foundation;

int main()
{
  init_apartment();
  Uri uri(L"http://aka.ms/cppwinrt");
  printf("Hello, %ls!\n", uri.AbsoluteUri().c_str());

  exprtk::parser<int> parser;
  exprtk::expression<int> expression;
  if (!parser.compile("true or false", expression))
  {
    printf("fail");
  }
  else
  {
    auto result = expression.value();
    printf("success");
  }
}
