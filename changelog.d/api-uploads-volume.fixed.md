The API chart did not mount a writable `/app/uploads`, so with the read-only
root filesystem Drogon logged 256 "Error 30 creating path ./uploads/tmp/NN/"
lines on every start and had nowhere to spool request bodies. The API
Deployment now mounts an `emptyDir` there, as the worker chart already did.
