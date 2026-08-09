# 그래프 알고리즘 모음 (Lumi 언어)

Lumi 언어로 구현한 그래프/경로 알고리즘 라이브러리와 데모 모음입니다.
모든 파일은 인터프리터로 바로 실행할 수 있습니다.

## 실행 방법

프로젝트 루트( `Lumina/` )에서:

```bash
# 라이브러리 + 데모 (각각 독립 실행)
c-interpreter\bin\lumi.exe examples\graph\demo_traversal.lumi      # BFS/DFS 순회
c-interpreter\bin\lumi.exe examples\graph\demo_shortest_path.lumi  # 다익스트라/FW/벨만포드
c-interpreter\bin\lumi.exe examples\graph\demo_mst.lumi            # Prim/Kruskal MST
c-interpreter\bin\lumi.exe examples\graph\demo_dag.lumi            # 위상정렬/사이클검출

# 인터랙티브 메뉴 (번호 입력으로 알고리즘 선택)
c-interpreter\bin\lumi.exe examples\graph\menu.lumi
```

## 파일 구성

| 파일 | 설명 |
|---|---|
| `graph.lumi` | **라이브러리 본체**. `Graph` 클래스 + 모든 알고리즘 함수. `bring` 으로 가져와 씁니다. |
| `demo_traversal.lumi` | BFS/DFS 그래프 순회와 최소 홉 경로 (지하철망 예시) |
| `demo_shortest_path.lumi` | 다익스트라, Floyd-Warshall, 벨만-포드 (도로망 예시) |
| `demo_mst.lumi` | Prim, Kruskal 최소 신장 트리 (마을 연결 예시, 두 알고리즘 결과 교차검증) |
| `demo_dag.lumi` | 위상정렬, 사이클 검출 (작업 스케줄링 / 순환 의존성 예시) |
| `menu.lumi` | 메뉴형 인터랙티브 데모 |

## 구현된 알고리즘

### 순회 / 경로
- `bfs(graph, start)` — 너비 우선 순회, 방문 순서 반환
- `dfs(graph, start)` — 깊이 우선 순회 (스택 기반)
- `dfsRecursive(graph, start)` — 재귀 DFS
- `bfsPath(graph, start, end)` — 가중치 없는 최단(최소 홉) 경로

### 최단 경로 (가중치)
- `dijkstra(graph, start)` — 음수 가중치 없는 그래프의 단일 출발 최단 경로. `{dist, prev}` 반환
- `dijkstraPath(graph, start, end)` — 다익스트라 결과로 경로 복원
- `bellmanFord(graph, start)` — 음수 가중치 허용 + 음수 사이클 검출. `{dist, prev, negCycle, negCycleNodes}` 반환
- `floydWarshall(graph)` — 모든 쌍 최단 경로. `{matrix, index, names}` 반환

### 최소 신장 트리 (무방향)
- `prim(graph, start)` — 정점 확장 기반 MST. `{weight, edges}` 반환
- `kruskal(graph)` — 간선 정렬 + Union-Find 기반 MST. `{weight, edges}` 반환

### 방향 그래프
- `topologicalSort(graph)` — Kahn 알고리즘 위상 정렬. `{order, hasCycle}` 반환
- `hasCycle(graph)` — DFS 색칠법 사이클 검출

### 보조
- `countComponents(graph)` — 연결 성분 개수 (무방향)
- `reconstructPath(prev, start, end)` — `prev` 맵으로 경로 복원
- `Graph` 클래스 — `addEdge`, `link`, `addNode`, `setDirected`, `nodes`, `neighbors`, `edges`, `show`
- `UnionFind` 클래스 — 경로압축 + rank 합 union-find

## 사용 예시 (내 프로그램에서 가져다 쓰기)

```lumi
bring graph up Graph, dijkstraPath

val g = Graph()
g.addEdge("A", "B", 5)
g.addEdge("B", "C", 3)
g.addEdge("A", "C", 10)

print(dijkstraPath(g, "A", "C"))   // ["A", "B", "C"]
```

## 구현 참고 사항 (Lumi 언어 특성)

- **`none` 리터럴은 실제 인터프리터에서 미지원** → "값 없음" 표식으로 빈 리스트 `[]` 를 사용했습니다 (`prim` 의 `bestV` 등). 정점값(문자열/숫자)과 절대 겹치지 않으므로 안전합니다.
- **우선순위 큐 대체**: `(거리, 정점)` 튜플 정렬 + 매 단계 최소 거리 정점 선형 탐색(O(V²)) 사용. 작은 그래프에서 간단하고 안정적입니다.
- **상태 공유 재귀**: 사이클 검출(`hasCycle`)은 클로저 변수 갱신 제약을 피하기 위해 `{"found": false}` dict 로 상태를 주고받는 helper 함수를 사용합니다.
- **`erase` 동작**: `erase(place, list)` 형태라 값으로 원소를 지울 때는 직접 필터링합니다 (`dijkstra` 의 `remaining` 제거 부분).
- **`bring` 동작**: 라이브러리는 `func`/`class`/`val` 정의만 노출하며, 같은 폴더의 `.lumi` 파일을 자동으로 찾습니다.
