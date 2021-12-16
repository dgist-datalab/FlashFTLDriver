# PLR 라이브러리

## 제공되는 함수

> PLR(int64\_t slope\_bit, int32\_t range);

> void insert(int32\_t LBA, int32\_t PPA);

> void insert\_end();

> uint64\_t memory\_usage();

> int64\_t get(int64\_t LBA);

> void clear();

## TODO

- ~양쪽 침범 범위 5\% 단위로 조정 가능하도록 하기~

- 기울기가 0과 1 사이의 범위로 표현 불가능할 때 선이 제대로 생성되지 않는 예외 처리

- ~델타 인코딩 bit로 표현 불가능할 때 끊고 새로 만드는 기능~

- ~적당하지 않은 Parameter로부터 발생되는 오류 감지하는 기능~

- 주석 추가
