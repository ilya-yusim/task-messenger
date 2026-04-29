// Package webassets exposes the embedded controller UI as an fs.FS.
// The embed directive must live in the same package as the web/
// directory it points at, which is why this file sits next to it.
package webassets

import (
	"embed"
	"io/fs"
)

//go:embed web
var raw embed.FS

// FS returns the embedded UI rooted at the directory that contains
// index.html (i.e. paths like "/index.html" map to "web/index.html").
func FS() fs.FS {
	sub, err := fs.Sub(raw, "web")
	if err != nil {
		// embed guarantees the directory exists at compile time.
		panic(err)
	}
	return sub
}
