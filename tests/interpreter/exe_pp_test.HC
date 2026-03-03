// skip-jit skip-aot — exe blocks सिर्फ interpreter में चलते हैं
#exe {
    Emit("I64 pp_var = 100;\n");
}

U0 Main()
{
    "pp_var = %d\n", pp_var;
}
