#!/bin/bash

while true; do
  curl http://127.0.0.1:8080 -H 'Content-Type: application/json' -d '{"name": "John", "age": 30}'
  sleep 0.05
done
