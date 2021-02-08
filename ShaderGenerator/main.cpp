#include "pch.h"
#include "ShaderConfiguration.h"
//#include "exprtk.hpp"

using namespace winrt;
using namespace ShaderGenerator;


int main()
{
  init_apartment();

  auto info = ShaderInfo::FromFile("D:\\cae\\dev\\ShaderCompiler\\ShaderGenerator\\ComputeShader.hlsl");
  auto permutations = ShaderOption::Permutate(info.Options);

  printf("asd");
  /*exprtk::parser<float> parser;
  exprtk::expression<float> expression;
  exprtk::symbol_table<float> symbols;
  symbols.add_constant("my_const", 2.f);
  expression.register_symbol_table(symbols);
  if (!parser.compile("(true and false) or my_const = 1", expression))
  {
    printf("fail");
  }
  else
  {
    auto result = expression.value();
    printf("success");
  }*/
}
