#!/usr/bin/python3

from os import environ

print("Content-Type: text/html")
print()
print("""
<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Value</th>
    </tr>
  </thead>
  <tbody>
""")

for x in environ.items():
    print("<tr><td>%s</td><td>%s</td></tr>" % x)

print("""
  </tbody>
</table>
""")
