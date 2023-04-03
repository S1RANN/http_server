#!/bin/bash

time for i in {1..100000}; do
  curl http://127.0.0.1:8080 -H 'Content-Type: application/json' -d '{"name": "John", "age": 30}'
done
