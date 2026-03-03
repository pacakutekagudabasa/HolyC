// skip-jit skip-aot — exe blocks सिर्फ interpreter में चलते हैं
exe {
    Emit("I64 generated_var = 42;\n");
}

U0 Main()
{
    "generated_var = %d\n", generated_var;
}
