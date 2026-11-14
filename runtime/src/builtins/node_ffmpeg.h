#ifndef NODE_FFMPEG_H
#define NODE_FFMPEG_H

#include "runtime.h"

/*
 * ffmpeg module -- thin wrappers around system ffmpeg/ffprobe binaries.
 *
 * Typical usage (TS):
 *   import * as ffmpeg from "ffmpeg";
 *   const r = ffmpeg.run(["-i", "in.mp4", "out.webm"]);
 *   const info = ffmpeg.probe("in.mp4");
 */

Value node_ffmpeg_run(Value args, Value options);
Value node_ffmpeg_exec(Value command, Value options);
Value node_ffmpeg_ffprobe(Value args, Value options);
Value node_ffmpeg_probe(Value file, Value options);
Value node_ffmpeg_version(void);
Value node_ffmpeg_isAvailable(void);

#endif /* NODE_FFMPEG_H */
