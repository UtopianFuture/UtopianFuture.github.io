## AMBA AXI Protocol Specification

### A1 Introduction

#### A1.2 AXI Architecture

The AXI protocol defines transactions that are used to read and write data, or control the caching of data and translations. Transactions are performed by sending transfers on the channels:
• Write request, which has signal names beginning with **AW**.
• Write data, which has signal names beginning with **W**.
• Write response, which has signal names beginning with **B**.
• Read request, which has signal names beginning with **AR**.
• Read data, which has signal names beginning with **R**.