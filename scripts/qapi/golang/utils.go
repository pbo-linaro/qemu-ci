/*
 * Copyright 2025 Red Hat, Inc.
 * SPDX-License-Identifier: MIT-0
 *
 * Authors:
 *  Victor Toso <victortoso@redhat.com>
 */
package qapi

import (
	"encoding/json"
	"fmt"
	"strings"
)

// Creates a decoder that errors on unknown Fields
// Returns nil if successfully decoded @from payload to @into type
// Returns error if failed to decode @from payload to @into type
func strictDecode(into interface{}, from []byte) error {
	dec := json.NewDecoder(strings.NewReader(string(from)))
	dec.DisallowUnknownFields()

	if err := dec.Decode(into); err != nil {
		return err
	}
	return nil
}

// This helper is used to move struct's fields into a map.
// This function is useful to merge JSON objects.
func unwrapToMap(m map[string]any, data any) error {
	if bytes, err := json.Marshal(&data); err != nil {
		return fmt.Errorf("unwrapToMap: %s", err)
	} else if err := json.Unmarshal(bytes, &m); err != nil {
		return fmt.Errorf("unwrapToMap: %s, data=%s", err, string(bytes))
	}
	return nil
}
