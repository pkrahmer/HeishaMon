# syntax=docker/dockerfile:1

FROM python:3.8-slim-buster

WORKDIR /app

COPY pyshamon/requirements.txt requirements.txt
RUN pip3 install -r pyshamon/requirements.txt

COPY pyshamon .

CMD ["python3", "pyshamon/pyshamon.py"]
