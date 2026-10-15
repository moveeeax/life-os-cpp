A JWT whose header or payload is not a JSON object, or whose `alg`/`typ` is
not a string, answers 401 (`malformed_header`, `unsupported_alg`,
`malformed_payload`) instead of throwing out of the auth middleware before
the signature check; the middleware also turns any exception from token
verification into 401 `malformed_token`.
