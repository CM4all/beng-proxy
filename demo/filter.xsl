<?xml version="1.0" encoding="UTF-8"?>

<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output encoding="UTF-8" method="html" indent="no"/>

  <xsl:template match="beng">
    <div>
      <xsl:apply-templates/>
    </div>
  </xsl:template>

  <xsl:template match="title">
    <h3>
      <xsl:apply-templates/>
    </h3>
  </xsl:template>

  <xsl:template match="paragraph">
    <p>
      <xsl:apply-templates/>
    </p>
  </xsl:template>

  <xsl:template match="buzzword">
    <em>
      <xsl:apply-templates/>
    </em>
  </xsl:template>
</xsl:stylesheet>
