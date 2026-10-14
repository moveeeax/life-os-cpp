An API key of an administrator keeps the data permissions (general, fitness
read, fitness sync) once its admin bit is masked: the Administrator role is
the sentinel bit alone, so 1.27.0 left such a key with no permission at all
and every fitness, workout and habits call answered 403.
