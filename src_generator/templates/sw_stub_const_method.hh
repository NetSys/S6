%RETURN_TYPE% %METHODNAME% (%ARGUMENTS%) %METHOD_CONST% {
  if (_is_const) {
    std::uint32_t _flag = %METHOD_FLAGS% ;
    char _args[%ARGSIZE%];
    %RETURN_DECL%

    %SET_ARGUMENTS%

    %RPC_STMT%

    %RETURN%
  } else {
    return _static_obj->%METHODNAME%(%ARGUMENTS_PASSING%);
  }
}
