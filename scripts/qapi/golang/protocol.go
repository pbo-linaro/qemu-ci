/*
 * Copyright 2025 Red Hat, Inc.
 * SPDX-License-Identifier: MIT-0
 *
 * Authors:
 *  Victor Toso <victortoso@redhat.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 */
package qapi

import (
	"encoding/json"
	"time"
)

/* Union of data for command, response, error, or event,
 * since when receiving we don't know upfront which we
 * must deserialize */
type Message struct {
	QMP       *json.RawMessage `json:"QMP,omitempty"`
	Execute   string           `json:"execute,omitempty"`
	ExecOOB   string           `json:"exec-oob,omitempty"`
	Event     string           `json:"event,omitempty"`
	Error     *json.RawMessage `json:"error,omitempty"`
	Return    *json.RawMessage `json:"return,omitempty"`
	ID        string           `json:"id,omitempty"`
	Timestamp *Timestamp       `json:"timestamp,omitempty"`
	Data      *json.RawMessage `json:"data,omitempty"`
	Arguments *json.RawMessage `json:"arguments,omitempty"`
}

type QAPIError struct {
	Class       string `json:"class"`
	Description string `json:"desc"`
}

func (err *QAPIError) Error() string {
	return err.Description
}

type Timestamp struct {
	Seconds      int `json:"seconds"`
	MicroSeconds int `json:"microseconds"`
}

func (t *Timestamp) AsTime() time.Time {
	return time.Unix(int64(t.Seconds), int64(t.MicroSeconds)*1000)
}
