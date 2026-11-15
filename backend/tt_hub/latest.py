"""Latest-only queue: a new item replaces the old one. Never builds a backlog."""

from __future__ import annotations

import asyncio
from typing import Generic, TypeVar

T = TypeVar("T")


class Latest(Generic[T]):
    def __init__(self) -> None:
        self._q: asyncio.Queue[T] = asyncio.Queue(maxsize=1)

    def put(self, item: T) -> None:
        if self._q.full():
            self._q.get_nowait()  # discard the stale item
        self._q.put_nowait(item)

    async def get(self) -> T:
        return await self._q.get()
