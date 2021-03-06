#!/usr/bin/env ruby
#
# Quick'n'dirty report script for the beng-proxy stopwatch.
#
# Usage: ruby stopwatch-stats.rb /var/log/cm4all/beng-proxy/current
#

# collect input data
data = {}
ARGF.each do |line|
    # remove timestamp
    line.sub!(/^@[0-9a-f]+ /, '')

    if line =~ /^stopwatch\[\w+\.cls\//
      line.sub!('.cls/', '.cls /')
    end

    line.sub!(/^(stopwatch\[)(clear-html)(\/.*)$/, '\1\2 /\2\3')

    # only stopwatch lines
    if line =~ /^stopwatch\[(\S+) ([^\]]+)\]: .*headers=(\d+)ms/
        host, uri, t = $1, $2, $3.to_i

        # no translation server stats
        next if host == '@translation' or host =~ /^localhost:/

        # remove some useless query parameters
        uri.gsub!(/(\/(?:config|preview|show|thumbnail)).*/, '\1')
        uri.gsub!(/[&?](_|txnid)=[\da-f]+/, '')
        uri.gsub!(/(?:beng_session|step|u|d|kc|k|\w+Id|max\w+|domain|path|address|companyname|type|guestbookId|plz|width|page_no|cc|wiid|effect|generic|search|firstResult|maxResults|descending|orderBy|header|project|parentStateId|_statemachineId|parentStatemachineId|color|trigger|direction|isWizard|preset|output|referrer|redirect|ctimestamp|name|email|entries_per_page|text|homepage|cm-[-\w]+)=[-\w.\+%\$]*/, '')
        uri.gsub!(/\/(pageId|skin|wcid|wiid|initParams|bodyonly|family)=[-\+\w.$]*/, '')
        uri.gsub!(/\?ticket=[\|0-9a-f]+/, '')

        # little bit of cleanup
        uri.gsub!(/&{2,}/, '&')
        uri.gsub!(/\/{2,}/, '/')
        uri.gsub!(/\?&/, '?')
        uri.gsub!(/[&?]$/, '')
        uri.sub!(/(=[^\/]*)\/$/, '\1')

        # some special cases
        uri.sub!(/((?:Widget|statistics)\/show|sam\/jump\.cls|photo\/(?:CONFIG|SHOW))\?.*/, '\1')
        uri.sub!(/(GuestbookPublic\.cls\?subaction=(?:addEntry|getList))&.*/, '\1')
        uri.sub!(/(Download\.cls)\?.*/, '\1')
        uri.sub!(/(wcid=cm4all.com.widgets.PhotoAlbum\/).*/, '\1')
        uri.sub!(/(SM_smID_\d+.cls).*/, '\1')
        uri.sub!(/(Editor\.cls\/action=[^\/]+).*/, '\1')
        uri.sub!(/(\/action=(?:getThumbnail|create))\/.*/, '\1')
        uri.sub!(/(\/res\/).*/, '\1')
        uri.sub!(/(\/pid)\/p\d+/, '\1')
        uri.sub!(/^(\/widgetserver\/.+\.action).*/, '\1')
        uri.sub!(/^(\/widgetserver\/\w+\/static\/).*/, '\1')
        uri.sub!(/^(\/login-service\/service\/(?:cred|i|sam)\/).*/, '\1')

        # remove random number hack
        uri.sub!(/\/\.(\d{4,})$/, '')

        # no static files
        next if uri =~ /\.(?:png|jpg|js|css|gif|conf|ico)(?:\?.*)?$/

        uri = host + uri if host =~ /\.cls$/
    elsif line =~ /^stopwatch\[(\/[^\]]+)\]: fork=.* end=(\d+)ms/
        uri, t = $1, $2.to_i
    else
        next
    end

    # insert this data point into the hash
    times = data[uri]
    if times
        times[0] += 1
        times[1] += t
        times[2] = t if t < times[2]
        times[3] = t if t > times[3]
    else
        data[uri] = [1, t, t, t]
    end
end

# calculate average time
l = []
data.each_pair do |uri, times|
    l << [times[1] / times[0], times[2], times[3], times[1], times[0], uri]
end

# sort the list by average
l.sort! do |a, b|
    b[0] <=> a[0]
end

# print it
puts "avg\tmin\tmax\ttotal\tcount\turi"
l.each do |i|
    puts i.join("\t")
end
