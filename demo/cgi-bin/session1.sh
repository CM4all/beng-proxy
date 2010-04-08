#!/bin/sh

echo "Content-Type: text/html"
echo
echo "$PATH_INFO<br>"
echo "<a c:base='widget' c:mode='focus' href='..'>..</a><br>"
echo "<a c:base='widget' c:mode='focus' href='foo/'>foo/</a><br>"
echo "<a c:base='widget' c:mode='focus' href='bar/'>bar/</a><br>"
