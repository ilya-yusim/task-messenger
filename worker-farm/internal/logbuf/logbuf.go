// Package logbuf manages per-worker log files. The implementation is
// deliberately simple: one file per worker, truncated on spawn, tailed
// via repeated reads. Rotation is a future enhancement.
package logbuf

import (
	"bufio"
	"context"
	"errors"
	"io"
	"os"
	"time"
)

// Open creates (truncating) the log file at path and returns it. The
// caller is responsible for closing it (typically by passing it as
// stdout/stderr to exec.Cmd; Go closes the file once Wait returns).
func Open(path string) (*os.File, error) {
	return os.OpenFile(path, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
}

// Tail returns up to maxBytes from the end of the file at path. If the
// file is shorter, returns the whole file. If maxBytes <= 0, returns
// the whole file.
func Tail(path string, maxBytes int64) ([]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return nil, err
	}
	size := st.Size()
	if maxBytes <= 0 || maxBytes >= size {
		return io.ReadAll(f)
	}
	if _, err := f.Seek(size-maxBytes, io.SeekStart); err != nil {
		return nil, err
	}
	return io.ReadAll(f)
}

// Stream sends every existing line from path, then polls for new
// content until ctx is cancelled. New bytes are written to w as soon as
// they appear (rounded to line boundaries when possible). Returns when
// ctx is done or the writer fails.
//
// Polling interval is 200 ms — too fast for any production service but
// fine for one operator on localhost.
func Stream(ctx context.Context, path string, w io.Writer) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()

	br := bufio.NewReader(f)
	const pollEvery = 200 * time.Millisecond
	ticker := time.NewTicker(pollEvery)
	defer ticker.Stop()

	for {
		// Drain whatever's available.
		for {
			line, err := br.ReadBytes('\n')
			if len(line) > 0 {
				if _, werr := w.Write(line); werr != nil {
					return werr
				}
				if f, ok := w.(interface{ Flush() error }); ok {
					_ = f.Flush()
				}
			}
			if err != nil {
				if errors.Is(err, io.EOF) {
					break
				}
				return err
			}
		}
		select {
		case <-ctx.Done():
			return nil
		case <-ticker.C:
		}
	}
}
